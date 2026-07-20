/**
 * @file camera.c
 * @brief Camera BSP — OV5647 MIPI CSI + ISP 驱动
 *
 * 硬件: OV5647 MIPI CSI 2-lane, ESP32-P4 MIPI CSI host
 * I2C:  共享 GT911 触摸屏的 I2C 总线 (GPIO7/8, I2C_NUM_1)
 * 参考: ESP-IDF examples/peripherals/camera/mipi_isp_dsi
 *
 * 数据流:
 *   OV5647 → MIPI CSI (RAW8) → ISP (RAW8→RGB565) → PSRAM frame buffer
 *
 * 依赖组件 (Component Registry):
 *   espressif/esp_cam_sensor: "^1.1.0"  — OV5647 传感器驱动 + SCCB
 *
 * 依赖组件 (ESP-IDF built-in):
 *   esp_driver_cam   — MIPI CSI 控制器 (esp_cam_ctlr_csi.h)
 *   esp_driver_isp   — ISP 处理器 (driver/isp.h)
 *   esp_driver_i2c   — I2C 驱动 (共享自 touch BSP)
 *
 * 画质增强:
 *   AE (Auto-Exposure)  — 自动曝光，防止画面偏暗/过曝
 *   AWB (Auto White Balance) — 自动白平衡，修正偏色
 */

#include "camera.h"
#include "bsp/touch/touch.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "driver/isp.h"
#include "driver/isp_ae.h"
#include "driver/isp_awb.h"
#include "esp_chip_info.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "esp_cam_sensor_detect.h"
#include "esp_cam_sensor.h"
#include "driver/jpeg_encode.h"
#include "sdkconfig.h"

static const char *TAG = "CAMERA";

/* ═══════════════════════════════════════════════
   Camera 硬件配置 (JC-ESP32P4-M3 开发板)
   ═══════════════════════════════════════════════ */

#define CAM_HRES                800
#define CAM_VRES                640
#define CAM_BYTES_PER_PIXEL     2           /* RGB565 = 2 bytes/pixel */
#define CAM_FRAME_BUF_SIZE      (CAM_HRES * CAM_VRES * CAM_BYTES_PER_PIXEL)  /* ~1.0 MB */

#define CAM_MIPI_LANE_BITRATE   200         /* Mbps (line_rate = pclk * 4) */
#define CAM_MIPI_DATA_LANES     2
#define CAM_ISP_CLK_HZ          (80 * 1000 * 1000)  /* 80 MHz */
#define CAM_SCCB_FREQ_HZ        (10 * 1000)         /* 10 kHz SCCB */

/* OV5647 可用格式 (匹配 sensor 实际支持的格式) */
#define CAM_FORMAT_NAME         "MIPI_2lane_24Minput_RAW8_800x640_50fps"

/* ═══════════════════════════════════════════════
   内部状态
   ═══════════════════════════════════════════════ */

static esp_cam_ctlr_handle_t    s_cam_handle    = NULL;
static isp_proc_handle_t        s_isp_proc      = NULL;
static esp_sccb_io_handle_t     s_sccb_handle   = NULL;
static esp_ldo_channel_handle_t s_ldo_mipi_phy  = NULL;
static SemaphoreHandle_t        s_frame_sem     = NULL;
/* CSI ping-pong 缓冲: 消除"DMA 边写 / CPU 边拷"撕裂。
 * on_get_new_trans 轮转交出下一块写入缓冲, on_trans_finished 发布刚完成的那块。
 * 3 块使得被消费的缓冲至少 2 个帧周期内不会被 DMA 复用, memcpy 有充足时间。 */
#define CAM_FB_COUNT            3
static uint8_t                 *s_frame_buf[CAM_FB_COUNT] = { NULL };
static volatile int             s_fb_get_idx    = 0;      /* 下一块交给 DMA 的缓冲下标 */
static uint8_t         *volatile s_fb_done       = NULL;   /* 最近完成的帧 (供消费者读取) */
static bool                     s_connected     = false;
static bool                     s_streaming     = false;
static isp_ae_ctlr_t            s_ae_ctlr       = NULL;
static isp_awb_ctlr_t           s_awb_ctlr      = NULL;
static jpeg_encoder_handle_t    s_jpeg_enc      = NULL;   /* JPEG 硬件编码器单例 (全生命周期复用) */

/* ═══════════════════════════════════════════════
   CSI 回调 — 帧传输完成通知
   ═══════════════════════════════════════════════ */

static IRAM_ATTR bool s_camera_get_new_vb(esp_cam_ctlr_handle_t handle,
                                           esp_cam_ctlr_trans_t *trans,
                                           void *user_data)
{
    /* 轮转分配下一帧写入缓冲 (ping-pong): DMA 写入的缓冲永远不是消费者正在拷贝的那块。
     * 仅访问静态数组 + 整数运算, IRAM 安全。 */
    int idx = s_fb_get_idx;
    trans->buffer = s_frame_buf[idx];
    trans->buflen = CAM_FRAME_BUF_SIZE;
    s_fb_get_idx = (idx + 1) % CAM_FB_COUNT;
    return false;
}

static IRAM_ATTR bool s_camera_get_finished_trans(esp_cam_ctlr_handle_t handle,
                                                   esp_cam_ctlr_trans_t *trans,
                                                   void *user_data)
{
    /* 发布刚完成的缓冲指针, 供 camera_get_latest_frame / camera_capture 读取 */
    s_fb_done = (uint8_t *)trans->buffer;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_frame_sem, &xHigherPriorityTaskWoken);
    return xHigherPriorityTaskWoken == pdTRUE;
}

/* ═══════════════════════════════════════════════
   传感器初始化 (SCCB over shared I2C)
   ═══════════════════════════════════════════════ */

static esp_err_t camera_sensor_init(void)
{
    esp_err_t ret = ESP_FAIL;

    /* 1. 获取触摸屏已创建的共享 I2C 总线 */
    i2c_master_bus_handle_t i2c_bus = touch_get_i2c_bus();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "Shared I2C bus not available (touch not initialized?)");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Using shared I2C bus from touch BSP");

    /* 2. 遍历所有已注册的传感器检测函数，自动探测 */
    esp_cam_sensor_config_t cam_config = {
        .reset_pin = -1,    /* 无独立 reset 引脚 */
        .pwdn_pin = -1,     /* 无独立 power-down 引脚 */
        .xclk_pin = -1,     /* 无独立 XCLK 引脚 (使用 MIPI 时钟) */
    };

    esp_cam_sensor_device_t *sensor = NULL;
    esp_cam_sensor_detect_fn_t *detector;

    for (detector = &__esp_cam_sensor_detect_fn_array_start;
         detector < &__esp_cam_sensor_detect_fn_array_end;
         ++detector) {

        /* 为每个候选传感器创建 SCCB IO 句柄 (重试不同 SCCB 地址) */
        sccb_i2c_config_t i2c_cfg = {
            .scl_speed_hz = CAM_SCCB_FREQ_HZ,
            .device_address = detector->sccb_addr,
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        };
        ret = sccb_new_i2c_io(i2c_bus, &i2c_cfg, &cam_config.sccb_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "sccb_new_i2c_io failed for addr 0x%02X: %s",
                     detector->sccb_addr, esp_err_to_name(ret));
            continue;
        }

        cam_config.sensor_port = detector->port;

        /* 尝试检测传感器 */
        sensor = (*(detector->detect))(&cam_config);
        if (sensor) {
            /* 确认接口类型匹配 (MIPI CSI) */
            if (detector->port != ESP_CAM_SENSOR_MIPI_CSI) {
                ESP_LOGW(TAG, "Sensor interface mismatch, skipping");
                esp_sccb_del_i2c_io(cam_config.sccb_handle);
                sensor = NULL;
                continue;
            }
            ESP_LOGI(TAG, "Camera sensor detected at SCCB addr 0x%02X",
                     detector->sccb_addr);
            s_sccb_handle = cam_config.sccb_handle;
            break;
        }

        /* 未检测到 → 释放 SCCB IO，尝试下一个 */
        esp_sccb_del_i2c_io(cam_config.sccb_handle);
    }

    if (!sensor) {
        ESP_LOGE(TAG, "No camera sensor detected. Check hardware connection.");
        return ESP_ERR_NOT_FOUND;
    }

    /* 3. 查询并选择传感器输出格式 */
    esp_cam_sensor_format_array_t fmt_array = {0};
    ret = esp_cam_sensor_query_format(sensor, &fmt_array);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Query format failed");
        return ret;
    }

    ESP_LOGI(TAG, "Available formats (%d):", fmt_array.count);
    for (int i = 0; i < fmt_array.count; i++) {
        ESP_LOGI(TAG, "  [%d] %s", i, fmt_array.format_array[i].name);
    }

    /* 查找目标格式 */
    const esp_cam_sensor_format_t *target_fmt = NULL;
    for (int i = 0; i < fmt_array.count; i++) {
        if (strcmp(fmt_array.format_array[i].name, CAM_FORMAT_NAME) == 0) {
            target_fmt = &fmt_array.format_array[i];
            break;
        }
    }

    if (!target_fmt) {
        /* 目标格式不可用，尝试第一个可用格式 */
        ESP_LOGW(TAG, "Target format '%s' not found, using first available: %s",
                 CAM_FORMAT_NAME, fmt_array.format_array[0].name);
        target_fmt = &fmt_array.format_array[0];
    }

    /* 4. 设置格式并启动传感器数据流 */
    ret = esp_cam_sensor_set_format(sensor, target_fmt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set format '%s' failed: %s", target_fmt->name,
                 esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Sensor format: %s", target_fmt->name);
    ESP_LOGI(TAG, "Sensor Bayer: %d (GBRG=%d, RGGB=%d, GRBG=%d, BGGR=%d)",
             (int)target_fmt->isp_info->isp_v1_info.bayer_type,
             (int)ESP_CAM_SENSOR_BAYER_GBRG,
             (int)ESP_CAM_SENSOR_BAYER_RGGB,
             (int)ESP_CAM_SENSOR_BAYER_GRBG,
             (int)ESP_CAM_SENSOR_BAYER_BGGR);

    int stream_enable = 1;
    ret = esp_cam_sensor_ioctl(sensor, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_enable);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start sensor stream failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Sensor stream started");

    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   ISP AE + AWB 初始化 (画质增强)
   ═══════════════════════════════════════════════ */

static esp_err_t camera_isp_ae_awb_init(void)
{
    esp_err_t ret;

    /* ── AE (自动曝光) 控制器 ──
       统计去马赛克后的亮度直方图, 自动调节曝光时间/增益,
       解决画面偏暗或过曝问题 */
    esp_isp_ae_config_t ae_cfg = {
        .sample_point = ISP_AE_SAMPLE_POINT_AFTER_DEMOSAIC,
        .window = {
            .top_left  = {.x = 32, .y = 32},
            .btm_right = {.x = CAM_HRES - 32, .y = CAM_VRES - 32},
        },
        .intr_priority = 0,
    };
    ret = esp_isp_new_ae_controller(s_isp_proc, &ae_cfg, &s_ae_ctlr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AE controller create failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_isp_ae_controller_enable(s_ae_ctlr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AE controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "AE controller: OK (window %dx%d)",
             ae_cfg.window.btm_right.x - ae_cfg.window.top_left.x,
             ae_cfg.window.btm_right.y - ae_cfg.window.top_left.y);

    /* ── AWB (自动白平衡) 控制器 ──
       统计 CCM 后的 RGB 比例, 自动校正色偏,
       解决室内灯光下画面偏黄/偏蓝问题

       注意: AWB 控制器需要 ESP32-P4 rev >= 3.0。
       低版本芯片不支持 AWB 控制器创建，直接跳过。 */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint32_t chip_rev = chip_info.revision;

    if (chip_rev < 300) {
        ESP_LOGW(TAG, "AWB skipped: chip rev %lu.%lu < 3.0 (not supported)",
                 chip_rev / 100, chip_rev % 100);
        ESP_LOGI(TAG, "AWB controller: SKIPPED (AE only)");
        return ESP_OK;  /* AE 已启用，AWB 跳过不算失败 */
    }

    esp_isp_awb_config_t awb_cfg = {
        .sample_point = ISP_AWB_SAMPLE_POINT_AFTER_CCM,
        .window = {
            .top_left  = {.x = 32, .y = 32},
            .btm_right = {.x = CAM_HRES - 32, .y = CAM_VRES - 32},
        },
        .subwindow = {
            .top_left  = {.x = 32, .y = 32},
            .btm_right = {.x = CAM_HRES - 32, .y = CAM_VRES - 32},
        },
        .white_patch = {
            .luminance       = {.min = 0, .max = 255 * 3},
            .red_green_ratio  = {.min = 0.0f, .max = 4.0f},
            .blue_green_ratio = {.min = 0.0f, .max = 4.0f},
        },
        .intr_priority = 0,
    };

    ret = esp_isp_new_awb_controller(s_isp_proc, &awb_cfg, &s_awb_ctlr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AWB controller create failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_isp_awb_controller_enable(s_awb_ctlr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AWB controller enable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "AWB controller: OK");

    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   公开接口
   ═══════════════════════════════════════════════ */

esp_err_t camera_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "─── Camera Init Start ───");

    /* 0. MIPI PHY LDO 供电 (参考 mipi_isp_dsi) */
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = 3,          /* LDO channel 3 for MIPI PHY */
        .voltage_mv = 2500,    /* 2.5V */
    };
    ret = esp_ldo_acquire_channel(&ldo_cfg, &s_ldo_mipi_phy);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MIPI PHY LDO acquire failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[0/5] MIPI PHY LDO: OK (ch=%d, %dmV)", ldo_cfg.chan_id, ldo_cfg.voltage_mv);

    /* 1. 传感器初始化 (SCCB over shared I2C) */
    ret = camera_sensor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[1/5] Sensor init FAILED: %s", esp_err_to_name(ret));
        goto fail_ldo;
    }
    ESP_LOGI(TAG, "[1/5] Sensor init: OK");

    /* 2. MIPI CSI 控制器配置 */
    esp_cam_ctlr_csi_config_t csi_config = {
        .ctlr_id = 0,
        .h_res = CAM_HRES,
        .v_res = CAM_VRES,
        .lane_bit_rate_mbps = CAM_MIPI_LANE_BITRATE,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
        .data_lane_num = CAM_MIPI_DATA_LANES,
        .byte_swap_en = false,
        .queue_items = 1,
    };
    ret = esp_cam_new_csi_ctlr(&csi_config, &s_cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[2/5] CSI controller init FAILED: %s", esp_err_to_name(ret));
        goto fail_sensor;
    }
    ESP_LOGI(TAG, "[2/5] CSI controller: OK (%dx%d, %d Mbps, %d lanes)",
             CAM_HRES, CAM_VRES, CAM_MIPI_LANE_BITRATE, CAM_MIPI_DATA_LANES);

    /* 3. 帧信号量 (用于同步帧捕获) */
    s_frame_sem = xSemaphoreCreateBinary();
    if (!s_frame_sem) {
        ESP_LOGE(TAG, "[3/5] Semaphore create FAILED");
        ret = ESP_ERR_NO_MEM;
        goto fail_csi;
    }

    /* 帧缓冲区 (PSRAM, 缓存对齐 — DMA 要求) */
    size_t buf_alignment = 0;
    ret = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &buf_alignment);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[3/5] Cache alignment query FAILED");
        goto fail_sem;
    }
    s_frame_buf[0] = NULL;  /* 供 fail_buf 统一清理 */
    bool alloc_ok = true;
    for (int i = 0; i < CAM_FB_COUNT; i++) {
        s_frame_buf[i] = heap_caps_aligned_calloc(buf_alignment, 1, CAM_FRAME_BUF_SIZE,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_frame_buf[i]) {
            ESP_LOGE(TAG, "[3/5] Frame buffer %d alloc FAILED (%zu bytes, align=%zu)",
                     i, (size_t)CAM_FRAME_BUF_SIZE, buf_alignment);
            alloc_ok = false;
            break;
        }
    }
    if (!alloc_ok) {
        ret = ESP_ERR_NO_MEM;
        goto fail_buf;
    }
    s_fb_get_idx = 0;
    s_fb_done    = NULL;
    ESP_LOGI(TAG, "[3/5] Frame buffers: %d x %zu bytes (PSRAM, align=%zu)",
             CAM_FB_COUNT, (size_t)CAM_FRAME_BUF_SIZE, buf_alignment);

    /* 注册 CSI 回调 */
    esp_cam_ctlr_trans_t init_trans = {
        .buffer = s_frame_buf[0],
        .buflen = CAM_FRAME_BUF_SIZE,
    };
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = s_camera_get_new_vb,
        .on_trans_finished = s_camera_get_finished_trans,
    };
    ret = esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, &init_trans);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[3/5] CSI callback register FAILED");
        goto fail_buf;
    }

    ret = esp_cam_ctlr_enable(s_cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[3/5] CSI enable FAILED");
        goto fail_buf;
    }
    ESP_LOGI(TAG, "[3/5] CSI callbacks + enable: OK");

    /* 4. ISP 处理器配置 (RAW8 → RGB565, 80 MHz) */
    esp_isp_processor_cfg_t isp_config = {
        .clk_hz = CAM_ISP_CLK_HZ,
        .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB565,
        .has_line_start_packet = false,
        .has_line_end_packet = false,
        .h_res = CAM_HRES,
        .v_res = CAM_VRES,
        .bayer_order = COLOR_RAW_ELEMENT_ORDER_GBRG,  /* OV5647 Bayer 为 GBRG, 若用默认 BGGR
                                                          则 demosaic 将 G 通道当作 B, 三通道
                                                          数据严重重叠 → 预览黑白/去饱和 */
        .flags.byte_swap_en = 1,  /* ISP 输出 RGB565 LE (与 esp_video 官方一致);
                                     硬件 JPEG 编码器不支持 BE 输入, 若不设此位必 crash */
    };
    ret = esp_isp_new_processor(&isp_config, &s_isp_proc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[4/5] ISP init FAILED: %s", esp_err_to_name(ret));
        goto fail_csi_enabled;
    }
    ret = esp_isp_enable(s_isp_proc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[4/5] ISP enable FAILED");
        goto fail_isp;
    }
    ESP_LOGI(TAG, "[4/5] ISP processor: OK (80 MHz, RAW8→RGB565, bayer=%d (GBRG=%d))",
             (int)isp_config.bayer_order, (int)COLOR_RAW_ELEMENT_ORDER_GBRG);

    /* 4.5 AE + AWB 画质增强 (非致命: 失败仍可预览, 但会偏暗/偏色) */
    ret = camera_isp_ae_awb_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[4.5/5] AE/AWB init FAILED — preview may be dark or color-shifted");
    } else {
        ESP_LOGI(TAG, "[4.5/5] AE + AWB: OK");
    }

    /* 4.6 JPEG 硬件编码器单例 — 全生命周期复用, 避免每次拍照 create/destroy 导致
     *     esp_driver_jpeg 内部 codec handle 状态异常 (P4 高频反复创建会概率性 NULL deref) */
    {
        jpeg_encode_engine_cfg_t jpeg_eng_cfg = {
            .intr_priority = 0,
            .timeout_ms    = 5000,
        };
        ret = jpeg_new_encoder_engine(&jpeg_eng_cfg, &s_jpeg_enc);
        if (ret != ESP_OK || !s_jpeg_enc) {
            ESP_LOGW(TAG, "[4.6/5] JPEG encoder singleton create FAILED: %s (0x%x) handle=%p — "
                     "JPEG encode will be unavailable",
                     esp_err_to_name(ret), ret, (void *)s_jpeg_enc);
            s_jpeg_enc = NULL;  /* 非致命, camera_rgb565_to_jpeg 会优雅失败 */
        } else {
            ESP_LOGI(TAG, "[4.6/5] JPEG encoder singleton: OK (handle=%p)", (void *)s_jpeg_enc);
        }
    }

    /* 5. 清空帧缓冲区并启动 CSI 流 */
    for (int i = 0; i < CAM_FB_COUNT; i++) {
        memset(s_frame_buf[i], 0x00, CAM_FRAME_BUF_SIZE);
        esp_cache_msync(s_frame_buf[i], CAM_FRAME_BUF_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }

    ret = esp_cam_ctlr_start(s_cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[5/5] CSI start FAILED: %s", esp_err_to_name(ret));
        goto fail_ae_awb;
    }
    s_streaming = true;
    ESP_LOGI(TAG, "[5/5] CSI streaming: STARTED");

    s_connected = true;
    ESP_LOGI(TAG, "─── Camera Init Complete ───");
    return ESP_OK;

    /* 错误回滚 */
fail_ae_awb:
    if (s_awb_ctlr) {
        esp_isp_awb_controller_disable(s_awb_ctlr);
        esp_isp_del_awb_controller(s_awb_ctlr);
        s_awb_ctlr = NULL;
    }
    if (s_ae_ctlr) {
        esp_isp_ae_controller_disable(s_ae_ctlr);
        esp_isp_del_ae_controller(s_ae_ctlr);
        s_ae_ctlr = NULL;
    }
    if (s_jpeg_enc) {
        jpeg_del_encoder_engine(s_jpeg_enc);
        s_jpeg_enc = NULL;
    }
fail_isp:
    if (s_isp_proc) {
        esp_isp_disable(s_isp_proc);
        esp_isp_del_processor(s_isp_proc);
        s_isp_proc = NULL;
    }
fail_csi_enabled:
    esp_cam_ctlr_disable(s_cam_handle);
fail_buf:
    for (int i = 0; i < CAM_FB_COUNT; i++) {
        if (s_frame_buf[i]) {
            heap_caps_free(s_frame_buf[i]);
            s_frame_buf[i] = NULL;
        }
    }
fail_sem:
    if (s_frame_sem) {
        vSemaphoreDelete(s_frame_sem);
        s_frame_sem = NULL;
    }
fail_csi:
    esp_cam_ctlr_del(s_cam_handle);
    s_cam_handle = NULL;
fail_sensor:
    if (s_sccb_handle) {
        esp_sccb_del_i2c_io(s_sccb_handle);
        s_sccb_handle = NULL;
    }
fail_ldo:
    if (s_ldo_mipi_phy) {
        esp_ldo_release_channel(s_ldo_mipi_phy);
        s_ldo_mipi_phy = NULL;
    }
    ESP_LOGE(TAG, "─── Camera Init FAILED ───");
    return ret;
}

esp_err_t camera_deinit(void)
{
    ESP_LOGI(TAG, "Camera deinit");

    s_connected = false;
    s_streaming = false;

    /* AE/AWB 控制器清理 */
    if (s_awb_ctlr) {
        esp_isp_awb_controller_disable(s_awb_ctlr);
        esp_isp_del_awb_controller(s_awb_ctlr);
        s_awb_ctlr = NULL;
    }
    if (s_ae_ctlr) {
        esp_isp_ae_controller_disable(s_ae_ctlr);
        esp_isp_del_ae_controller(s_ae_ctlr);
        s_ae_ctlr = NULL;
    }

    /* JPEG 编码器单例清理 */
    if (s_jpeg_enc) {
        jpeg_del_encoder_engine(s_jpeg_enc);
        s_jpeg_enc = NULL;
        ESP_LOGI(TAG, "JPEG encoder singleton destroyed");
    }

    if (s_cam_handle) {
        esp_cam_ctlr_stop(s_cam_handle);
        esp_cam_ctlr_disable(s_cam_handle);
        esp_cam_ctlr_del(s_cam_handle);
        s_cam_handle = NULL;
    }

    if (s_isp_proc) {
        esp_isp_disable(s_isp_proc);
        esp_isp_del_processor(s_isp_proc);
        s_isp_proc = NULL;
    }

    if (s_frame_buf[0] || s_frame_buf[1] || s_frame_buf[2]) {
        for (int i = 0; i < CAM_FB_COUNT; i++) {
            if (s_frame_buf[i]) {
                heap_caps_free(s_frame_buf[i]);
                s_frame_buf[i] = NULL;
            }
        }
    }
    s_fb_done = NULL;

    if (s_frame_sem) {
        vSemaphoreDelete(s_frame_sem);
        s_frame_sem = NULL;
    }

    if (s_sccb_handle) {
        esp_sccb_del_i2c_io(s_sccb_handle);
        s_sccb_handle = NULL;
    }

    if (s_ldo_mipi_phy) {
        esp_ldo_release_channel(s_ldo_mipi_phy);
        s_ldo_mipi_phy = NULL;
    }

    ESP_LOGI(TAG, "Camera deinit complete");
    return ESP_OK;
}

esp_err_t camera_capture(uint8_t **data, size_t *len)
{
    if (!s_connected || !s_streaming) {
        ESP_LOGE(TAG, "Camera not ready");
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || !len) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Waiting for frame...");

    /* 等待 CSI 回调通知帧传输完成 (超时 5 秒) */
    if (xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Frame capture timeout (5s)");
        return ESP_ERR_TIMEOUT;
    }

    /* 停止 CSI 流 — 防止 DMA 在读取时覆盖缓冲区 */
    esp_cam_ctlr_stop(s_cam_handle);

    /* 读取最近完成的那块缓冲 (ping-pong 发布) */
    uint8_t *done = s_fb_done ? s_fb_done : s_frame_buf[0];

    /* 确保 CPU cache 与 PSRAM 同步 */
    esp_cache_msync(done, CAM_FRAME_BUF_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

    /* 拷贝帧到新缓冲区 (调用方负责释放) */
    uint8_t *copy = heap_caps_malloc(CAM_FRAME_BUF_SIZE,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        ESP_LOGE(TAG, "Frame copy alloc failed");
        esp_cam_ctlr_start(s_cam_handle);  /* 恢复流 */
        return ESP_ERR_NO_MEM;
    }

    memcpy(copy, done, CAM_FRAME_BUF_SIZE);
    *data = copy;
    *len = CAM_FRAME_BUF_SIZE;

    /* 恢复 CSI 流 (用于下一次拍照) */
    esp_cam_ctlr_start(s_cam_handle);

    ESP_LOGI(TAG, "Frame captured: %zu bytes (RGB565)", *len);
    return ESP_OK;
}

uint16_t camera_get_width(void)
{
    return CAM_HRES;
}

uint16_t camera_get_height(void)
{
    return CAM_VRES;
}

bool camera_is_connected(void)
{
    return s_connected;
}

/* ═══════════════════════════════════════════════
   连续采集模式 (实时预览用)
   ═══════════════════════════════════════════════ */

static bool s_streaming_mode = false;  /* true=连续采集, false=单帧 */

esp_err_t camera_start_streaming(void)
{
    if (!s_connected || !s_cam_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_streaming_mode) {
        return ESP_OK;  /* 已在连续模式 */
    }

    /* 先尝试停止可能残留的 CSI 流，避免 INVALID_STATE */
    esp_cam_ctlr_stop(s_cam_handle);

    /* 确保 CSI 流已启动 */
    esp_err_t ret = esp_cam_ctlr_start(s_cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start streaming: %s", esp_err_to_name(ret));
        return ret;
    }

    s_streaming_mode = true;
    ESP_LOGI(TAG, "Streaming started");
    return ESP_OK;
}

esp_err_t camera_stop_streaming(void)
{
    if (!s_streaming_mode) {
        return ESP_OK;
    }

    esp_err_t ret = esp_cam_ctlr_stop(s_cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop streaming: %s", esp_err_to_name(ret));
        return ret;
    }

    s_streaming_mode = false;
    ESP_LOGI(TAG, "Streaming stopped");
    return ESP_OK;
}

esp_err_t camera_get_latest_frame(uint8_t **data, size_t *len)
{
    if (!s_connected || !s_streaming_mode) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || !len) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 等待新帧 (非阻塞，使用已有信号量) */
    if (xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;  /* 无新帧，使用上一帧 */
    }

    /* 不停止 CSI 流！读取最近完成的那块 (ping-pong: DMA 正在写另一块, 拷贝安全) */
    uint8_t *done = s_fb_done;
    if (!done) {
        return ESP_ERR_INVALID_STATE;  /* 尚无完成帧 */
    }
    esp_cache_msync(done, CAM_FRAME_BUF_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

    uint8_t *copy = heap_caps_malloc(CAM_FRAME_BUF_SIZE,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        ESP_LOGE(TAG, "Frame copy alloc failed");
        return ESP_ERR_NO_MEM;
    }

    memcpy(copy, done, CAM_FRAME_BUF_SIZE);
    *data = copy;
    *len = CAM_FRAME_BUF_SIZE;

    /* 前 5 帧诊断: 打印 RGB565 前 16 字节, 验证颜色分量不全是等值(非灰度) */
    {
        static int s_sample_cnt = 0;
        if (s_sample_cnt < 5) {
            const uint8_t *p = copy;
            ESP_LOGI(TAG, "RGB565[#%d] %02x%02x %02x%02x %02x%02x %02x%02x "
                     "%02x%02x %02x%02x %02x%02x %02x%02x",
                     s_sample_cnt,
                     p[0],p[1], p[2],p[3], p[4],p[5], p[6],p[7],
                     p[8],p[9], p[10],p[11], p[12],p[13], p[14],p[15]);
            s_sample_cnt++;
        }
    }

    return ESP_OK;
}

bool camera_is_streaming(void)
{
    return s_streaming_mode;
}

/* ═══════════════════════════════════════════════
   RGB565 → JPEG 硬件编码 (ESP32-P4 esp_driver_jpeg)
   ═══════════════════════════════════════════════ */

esp_err_t camera_rgb565_to_jpeg(const uint8_t *rgb565, size_t rgb565_len,
                                 uint8_t **jpeg_out, size_t *jpeg_len)
{
    if (!rgb565 || !rgb565_len || !jpeg_out || !jpeg_len) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_jpeg_enc) {
        ESP_LOGE(TAG, "JPEG encoder singleton not available "
                 "(camera_init JPEG step may have failed)");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "JPEG encode: RGB565 %zu bytes, src=%p, enc=%p, %dx%d",
             rgb565_len, (const void *)rgb565, (void *)s_jpeg_enc,
             CAM_HRES, CAM_VRES);

    /* ── 编码配置 (RGB565 直接输入，硬件支持) ── */
    jpeg_encode_cfg_t enc_cfg = {
        .width         = CAM_HRES,
        .height        = CAM_VRES,
        .src_type      = JPEG_ENCODE_IN_FORMAT_RGB565,
        .sub_sample    = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = 20,   /* Q20: 上传约 20-30KB, OCR 足够 */
    };

    /* ── 分配输出缓冲区 (必须缓存对齐 — HW 要求) ── */
    size_t out_buf_size = rgb565_len / 4;  /* JPEG 压缩比通常 >4:1 */
    if (out_buf_size < 4096) out_buf_size = 4096;
    jpeg_encode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    size_t actual_alloc_size = 0;
    uint8_t *out_buf = jpeg_alloc_encoder_mem(out_buf_size, &mem_cfg, &actual_alloc_size);
    if (!out_buf) {
        ESP_LOGE(TAG, "JPEG output buffer alloc failed (%zu bytes)", out_buf_size);
        return ESP_ERR_NO_MEM;
    }

    /* ── 硬件编码 (复用单例 encoder, one-shot: RGB565 → JPEG) ── */
    uint32_t actual_out_size = 0;
    esp_err_t ret = jpeg_encoder_process(s_jpeg_enc, &enc_cfg, rgb565,
                                (uint32_t)rgb565_len,
                                out_buf, (uint32_t)out_buf_size, &actual_out_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG encode failed: %s (0x%x), enc=%p",
                 esp_err_to_name(ret), ret, (void *)s_jpeg_enc);
        free(out_buf);
        return ret;
    }

    ESP_LOGI(TAG, "JPEG encode OK: %zu bytes RGB565 → %lu bytes JPEG (Q=20, %.1f:1)",
             rgb565_len, (unsigned long)actual_out_size,
             (double)rgb565_len / (double)actual_out_size);

    *jpeg_out  = out_buf;
    *jpeg_len  = (size_t)actual_out_size;
    return ESP_OK;
}