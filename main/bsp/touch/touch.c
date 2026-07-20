/**
 * @file touch.c
 * @brief Touch BSP 实现 — GT911 电容触摸 (ESP32-P4)
 *
 * 硬件: JC-ESP32P4-M3 (GUTION)
 * 触摸接口: FPC 0.5 6P 抽屉下接
 *   - Pin3: RTC_DAT/SDA1 → GPIO7 (SDA)
 *   - Pin4: RTC_CLK/SCL1 → GPIO8 (SCL)
 *   - Pin5: TOUCH_INT     → GPIO21 (INT)
 *   - Pin6: TOUCH_RST     → GPIO22 (RST)
 *   - 无外部上拉电阻, 启用芯片内部上拉 (与 wifi_scan 验证配置一致)
 *
 * 驱动: espressif/esp_lcd_touch_gt911 (官方组件 v2.0+)
 * I2C 地址: 0x14 (7-bit, GT911 secondary: INT=HIGH at power-on)
 */

#include "touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TOUCH";

/* ========== 硬件配置 ========== */
#define TOUCH_I2C_SCL_GPIO      8
#define TOUCH_I2C_SDA_GPIO      7
#define TOUCH_I2C_ADDR          0x14
#define TOUCH_RST_GPIO          22
#define TOUCH_INT_GPIO          21
#define LCD_H_RES               1024
#define LCD_V_RES               600


/* ========== 内部句柄 ========== */
static i2c_master_bus_handle_t  s_i2c_bus  = NULL;
static esp_lcd_panel_io_handle_t s_io      = NULL;
static esp_lcd_touch_handle_t    s_touch   = NULL;

/* 错误恢复状态 */
static uint8_t  s_err_cnt     = 0;      /* 连续错误计数 */
static uint32_t s_err_tick    = 0;      /* 上次错误 tick */
static bool     s_bus_ok      = true;   /* 总线健康状态 */

/* ========== 内部辅助函数 ========== */

static void touch_cleanup(void)
{
    if (s_touch) {
        esp_lcd_touch_del(s_touch);
        s_touch = NULL;
    }
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}

/* ========== 公开接口 ========== */

esp_err_t touch_init(void)
{
    ESP_LOGI(TAG, "Initializing GT911 (JC-ESP32P4-M3)");
    ESP_LOGI(TAG, "  I2C SCL: GPIO%d, SDA: GPIO%d", TOUCH_I2C_SCL_GPIO, TOUCH_I2C_SDA_GPIO);
    ESP_LOGI(TAG, "  RST: GPIO%d, INT: GPIO%d", TOUCH_RST_GPIO, TOUCH_INT_GPIO);
    ESP_LOGI(TAG, "  I2C Addr: 0x%02X, Screen: %dx%d", TOUCH_I2C_ADDR, LCD_H_RES, LCD_V_RES);

    /* 预配置 INT 引脚为输出高电平，确保 GT911 上电时 INT=HIGH，选择 0x14 地址 */
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << TOUCH_INT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_cfg);
    gpio_set_level(TOUCH_INT_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* 回读验证 INT 电平 */
    int int_level = gpio_get_level(TOUCH_INT_GPIO);
    ESP_LOGI(TAG, "INT GPIO%d level after set: %d (expect 1 for addr 0x%02X)",
             TOUCH_INT_GPIO, int_level, TOUCH_I2C_ADDR);
    if (int_level != 1) {
        ESP_LOGW(TAG, "INT is LOW! GT911 may use addr 0x5D instead of 0x%02X",
                 TOUCH_I2C_ADDR);
    }

    /* 1. 创建 I2C master 总线 */
    i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_NUM_1,
        .scl_io_num = TOUCH_I2C_SCL_GPIO,
        .sda_io_num = TOUCH_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 15,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return ret;
    }
    ESP_LOGI(TAG, "I2C master bus created on port %d", I2C_NUM_1);

    /* 扫描 I2C 总线确认 GT911 实际地址 */
    ESP_LOGI(TAG, "Scanning I2C bus for GT911...");
    esp_err_t ret_14 = i2c_master_probe(s_i2c_bus, 0x14, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "  Probe 0x14: %s", ret_14 == ESP_OK ? "ACK ✓" : "NACK ✗");
    esp_err_t ret_5d = i2c_master_probe(s_i2c_bus, 0x5D, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "  Probe 0x5D: %s", ret_5d == ESP_OK ? "ACK ✓" : "NACK ✗");

    /* 根据扫描结果选择实际地址 */
    uint8_t actual_addr = TOUCH_I2C_ADDR;
    if (ret_14 != ESP_OK && ret_5d == ESP_OK) {
        actual_addr = 0x5D;
        ESP_LOGW(TAG, "GT911 detected at addr 0x5D, using 0x5D instead of 0x%02X", TOUCH_I2C_ADDR);
    } else if (ret_14 != ESP_OK && ret_5d != ESP_OK) {
        ESP_LOGE(TAG, "GT911 not found at either 0x14 or 0x5D!");
        ret = ESP_ERR_NOT_FOUND;
        goto fail_bus;
    }

    /* 2. 创建 I2C I/O 句柄 */
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = actual_addr;
    io_cfg.scl_speed_hz = 400000;  /* GT911 标准 Fast-mode 400kHz */

    ret = esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &s_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C panel IO create failed: %s (0x%x)", esp_err_to_name(ret), ret);
        goto fail_bus;
    }

    /* 3. GT911 专用配置 */
    esp_lcd_touch_io_gt911_config_t gt911_io_cfg = {
        .dev_addr = actual_addr,
    };

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,  /* 必须有效: INT 引脚电平决定 I2C 地址, 且标准路径有 50ms 复位等待 */
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy  = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .driver_data = &gt911_io_cfg,
    };

    /* 4. 创建 GT911 触摸句柄 */
    ret = esp_lcd_touch_new_i2c_gt911(s_io, &tp_cfg, &s_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GT911 create failed: %s (0x%x)", esp_err_to_name(ret), ret);
        goto fail_io;
    }

    ESP_LOGI(TAG, "Touch init complete, handle: %p", (void *)s_touch);

    /* GT911 INT 引脚为开漏输出，必须启用内部上拉，否则中断无法触发。
     * 驱动初始化完成后，将 INT 配置为输入并启用上拉，等待中断信号。 */
    gpio_set_direction(TOUCH_INT_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TOUCH_INT_GPIO, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "INT GPIO%d configured as input with pull-up", TOUCH_INT_GPIO);

    return ESP_OK;

fail_io:
    esp_lcd_panel_io_del(s_io);
    s_io = NULL;
fail_bus:
    i2c_del_master_bus(s_i2c_bus);
    s_i2c_bus = NULL;
    return ret;
}

esp_err_t touch_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing touch...");
    touch_cleanup();
    ESP_LOGI(TAG, "Touch deinitialized");
    return ESP_OK;
}

esp_lcd_touch_handle_t touch_get_handle(void)
{
    return s_touch;
}

i2c_master_bus_handle_t touch_get_i2c_bus(void)
{
    return s_i2c_bus;
}

/* 尝试恢复 I2C 总线 */
static void touch_bus_recovery(void)
{
    ESP_LOGW(TAG, "I2C bus recovery attempt...");

    /* 1. 删除现有设备句柄 */
    if (s_touch) {
        esp_lcd_touch_del(s_touch);
        s_touch = NULL;
    }
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }

    /* 2. 发送时钟脉冲尝试释放总线 (SCL 手动翻转) */
    gpio_set_direction(TOUCH_I2C_SCL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(TOUCH_I2C_SDA_GPIO, GPIO_MODE_INPUT);
    for (int i = 0; i < 16; i++) {
        gpio_set_level(TOUCH_I2C_SCL_GPIO, 1);
        esp_rom_delay_us(5);
        gpio_set_level(TOUCH_I2C_SCL_GPIO, 0);
        esp_rom_delay_us(5);
    }

    /* 3. 发送 STOP 条件 (SCL=1, SDA=0→1) */
    gpio_set_level(TOUCH_I2C_SCL_GPIO, 1);
    esp_rom_delay_us(5);
    gpio_set_direction(TOUCH_I2C_SDA_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(TOUCH_I2C_SDA_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(TOUCH_I2C_SCL_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(TOUCH_I2C_SCL_GPIO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(TOUCH_I2C_SDA_GPIO, 1);
    esp_rom_delay_us(5);

    /* 4. 恢复 GPIO 为开漏/输入模式，让 I2C 驱动接管 */
    gpio_set_direction(TOUCH_I2C_SCL_GPIO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(TOUCH_I2C_SDA_GPIO, GPIO_MODE_INPUT_OUTPUT_OD);

    /* 5. 重新创建 panel IO */
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = TOUCH_I2C_ADDR;
    io_cfg.scl_speed_hz = 400000;  /* GT911 标准 Fast-mode 400kHz */

    esp_err_t ret = esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &s_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bus recovery: panel IO recreate failed");
        s_bus_ok = false;
        return;
    }

    /* 6. 重新创建 GT911 句柄 */
    esp_lcd_touch_io_gt911_config_t gt911_io_cfg = {
        .dev_addr = TOUCH_I2C_ADDR,
    };
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TOUCH_RST_GPIO,
        .int_gpio_num = TOUCH_INT_GPIO,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags   = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
        .driver_data = &gt911_io_cfg,
    };

    ret = esp_lcd_touch_new_i2c_gt911(s_io, &tp_cfg, &s_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bus recovery: GT911 recreate failed");
        s_bus_ok = false;
        return;
    }

    gpio_set_direction(TOUCH_INT_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TOUCH_INT_GPIO, GPIO_PULLUP_ONLY);

    s_err_cnt = 0;
    s_bus_ok  = true;
    ESP_LOGI(TAG, "I2C bus recovery success");
}

esp_err_t touch_read(touch_point_t *point)
{
    if (!point) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_touch || !s_bus_ok) {
        point->pressed = 0;
        return ESP_ERR_INVALID_STATE;
    }

    /* 连续错误退避：错误后 500ms 内跳过读取，避免阻塞 LVGL */
    if (s_err_cnt > 0 && ((xTaskGetTickCount() - s_err_tick) * portTICK_PERIOD_MS) < 500) {
        point->pressed = 0;
        return ESP_ERR_NOT_FOUND;
    }

    /* 非阻塞读取 */
    esp_err_t ret = esp_lcd_touch_read_data(s_touch);
    if (ret != ESP_OK) {
        s_err_cnt++;
        s_err_tick = xTaskGetTickCount();
        ESP_LOGW(TAG, "Read data failed (%d): %s", s_err_cnt, esp_err_to_name(ret));

        /* 连续 3 次错误尝试恢复总线 */
        if (s_err_cnt >= 3) {
            touch_bus_recovery();
        }
        point->pressed = 0;
        return ESP_ERR_NOT_FOUND;
    }

    /* 成功则清零错误计数 */
    if (s_err_cnt > 0) {
        s_err_cnt = 0;
        ESP_LOGI(TAG, "I2C read recovered");
    }

    /* 获取坐标（统一使用 esp_lcd_touch_get_data） */
    esp_lcd_touch_point_data_t data[1];
    uint8_t cnt = 0;

    ret = esp_lcd_touch_get_data(s_touch, data, &cnt, 1);
    if (ret == ESP_OK && cnt > 0) {
        point->x        = data[0].x;
        point->y        = data[0].y;
        point->strength = data[0].strength;
        point->track_id = data[0].track_id;
        point->pressed  = 1;

        ESP_LOGD(TAG, "Touch: x=%d, y=%d, strength=%d, id=%d",
                 point->x, point->y, point->strength, point->track_id);
        return ESP_OK;
    }

    point->pressed = 0;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t touch_read_multi(touch_points_t *points)
{
    if (!points) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_touch || !s_bus_ok) {
        points->num = 0;
        return ESP_ERR_INVALID_STATE;
    }

    /* 连续错误退避 */
    if (s_err_cnt > 0 && ((xTaskGetTickCount() - s_err_tick) * portTICK_PERIOD_MS) < 500) {
        points->num = 0;
        return ESP_ERR_NOT_FOUND;
    }

    /* 非阻塞读取 */
    esp_err_t ret = esp_lcd_touch_read_data(s_touch);
    if (ret != ESP_OK) {
        s_err_cnt++;
        s_err_tick = xTaskGetTickCount();
        ESP_LOGW(TAG, "Multi read failed (%d): %s", s_err_cnt, esp_err_to_name(ret));
        if (s_err_cnt >= 3) {
            touch_bus_recovery();
        }
        points->num = 0;
        return ESP_ERR_NOT_FOUND;
    }

    if (s_err_cnt > 0) {
        s_err_cnt = 0;
        ESP_LOGI(TAG, "I2C multi-read recovered");
    }

    /* 获取所有坐标（统一使用 esp_lcd_touch_get_data） */
    esp_lcd_touch_point_data_t data[TOUCH_MAX_POINTS];
    uint8_t cnt = 0;

    ret = esp_lcd_touch_get_data(s_touch, data, &cnt, TOUCH_MAX_POINTS);
    if (ret == ESP_OK && cnt > 0) {
        points->num = (cnt > TOUCH_MAX_POINTS) ? TOUCH_MAX_POINTS : cnt;

        for (int i = 0; i < points->num; i++) {
            points->points[i].x        = data[i].x;
            points->points[i].y        = data[i].y;
            points->points[i].strength = data[i].strength;
            points->points[i].track_id = data[i].track_id;
            points->points[i].pressed  = 1;
        }

        ESP_LOGD(TAG, "Multi-touch: %d points", points->num);
        return ESP_OK;
    }

    points->num = 0;
    return ESP_ERR_NOT_FOUND;
}