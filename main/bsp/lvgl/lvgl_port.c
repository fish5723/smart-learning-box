/**
 * @brief LVGL Port — esp_lvgl_adapter 封装层
 *
 * 将 esp_lvgl_adapter 的注册 API 封装为项目通用的 lvgl_port_init() 入口，
 * 返回 display 和 indev句柄供 app 层使用。
 */

#include "lvgl_port.h"
#include "bsp/lcd/lcd.h"
#include "bsp/touch/touch.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_display.h"
#include "esp_lv_adapter_input.h"
#include "esp_log.h"
#include "esp_check.h"
#include "lvgl.h"

static const char *TAG = "LVGL_PORT";

/* ── 全局句柄 ── */
static lv_display_t *s_disp = NULL;
static lv_indev_t   *s_indev = NULL;

/* 触摸状态缓存（用于节流和错误保持） */
static lv_indev_data_t s_last_touch = {0};
static uint32_t s_last_read_ms = 0;

/* 轮询模式触摸读取回调 — 带 16ms 节流和非阻塞错误处理 */
static void lvgl_port_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint32_t now = lv_tick_get();

    /* 节流：每 16ms 最多读取一次（匹配60FPS帧时间，降低I2C轮询频率） */
    if (now - s_last_read_ms < 16) {
        *data = s_last_touch;
        return;
    }
    s_last_read_ms = now;

    touch_point_t point;
    esp_err_t ret = touch_read(&point);

    if (ret == ESP_OK && point.pressed) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        /* I2C 错误或无触摸：保持释放状态，不阻塞 LVGL */
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* 缓存本次状态 */
    s_last_touch = *data;
}

esp_err_t lvgl_port_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL port via esp_lvgl_adapter");

    /* ── 旋转 & 防撕裂模式（与 lcd.c 保持一致） ── */
#ifdef CONFIG_SMARTBOX_LCD_ROTATE_0
    esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
#elif defined(CONFIG_SMARTBOX_LCD_ROTATE_90)
    esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_90;
#elif defined(CONFIG_SMARTBOX_LCD_ROTATE_180)
    esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_180;
#elif defined(CONFIG_SMARTBOX_LCD_ROTATE_270)
    esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_270;
#else
    esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
#endif

#ifdef CONFIG_SMARTBOX_LCD_TEAR_AVOID_NONE
    esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
#elif defined(CONFIG_SMARTBOX_LCD_TEAR_AVOID_DOUBLE_DIRECT)
    esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_DIRECT;
#elif defined(CONFIG_SMARTBOX_LCD_TEAR_AVOID_DOUBLE_FULL)
    esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DOUBLE_FULL;
#elif defined(CONFIG_SMARTBOX_LCD_TEAR_AVOID_TRIPLE_PARTIAL)
    esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL;
#elif defined(CONFIG_SMARTBOX_LCD_TEAR_AVOID_TRIPLE_FULL)
    esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_FULL;
#else
    esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI;
#endif

    /* 1. 初始化 adapter（栈从默认 8KB 扩到 16KB，复杂 UI 页面需要更深调用栈） */
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_cfg.task_stack_size = 16384;
    ESP_RETURN_ON_ERROR(esp_lv_adapter_init(&adapter_cfg), TAG,
                        "Adapter init failed");

    /* 2. 注册显示（MIPI DSI + PSRAM 双缓冲） */
    esp_lv_adapter_display_profile_t profile = {
        .interface             = ESP_LV_ADAPTER_PANEL_IF_MIPI_DSI,
        .rotation              = rotation,
        .hor_res               = LCD_WIDTH,
        .ver_res               = LCD_HEIGHT,
        .buffer_height         = 120,
        .use_psram             = true,
        .enable_ppa_accel      = false,  /* 暂时禁用：PPA需要LV_DRAW_SW_DRAW_UNIT_CNT==1，当前=2 */
        .require_double_buffer = true,
        .mono_layout           = ESP_LV_ADAPTER_MONO_LAYOUT_NONE,
    };

    esp_lv_adapter_display_config_t disp_cfg = {
        .panel           = lcd_get_panel(),
        .panel_io        = lcd_get_panel_io(),
        .profile         = profile,
        .tear_avoid_mode = tear_mode,
        .te_sync         = ESP_LV_ADAPTER_TE_SYNC_DISABLED(),
    };

    s_disp = esp_lv_adapter_register_display(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "Display registration failed");
    ESP_LOGI(TAG, "Display registered: %dx%d", LCD_WIDTH, LCD_HEIGHT);

    /* 3. 注册触摸 (轮询模式，绕过中断问题) */
#if CONFIG_SMARTBOX_TOUCH_ENABLED
    esp_lcd_touch_handle_t touch_handle = touch_get_handle();
    ESP_LOGI(TAG, "Touch handle: %p", touch_handle);
    if (touch_handle) {
        /* 手动注册 LVGL 输入设备，使用轮询模式 */
        s_indev = lv_indev_create();
        if (s_indev) {
            lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_read_cb(s_indev, lvgl_port_touch_read);
            lv_indev_set_display(s_indev, s_disp);
            ESP_LOGI(TAG, "Touch registered (polling mode)");
        } else {
            ESP_LOGE(TAG, "Touch indev create failed");
        }
    }
#endif

    /* 4. 启动 adapter 任务 */
    ESP_RETURN_ON_ERROR(esp_lv_adapter_start(), TAG, "Adapter start failed");
    ESP_LOGI(TAG, "LVGL port init complete");
    return ESP_OK;
}