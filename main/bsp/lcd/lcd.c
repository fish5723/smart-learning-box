/**
 * @brief LCD BSP 实现 — JD9165BA + MIPI DSI
 *
 * 参考官方示例: peripherals/lcd/mipi_dsi/main/mipi_dsi_lcd_example_main.c
 * 验证基准: E:\wifi_scan (WiFi/LCD/Touch 全部验证通过)
 *
 * Factory timing: DOTCLK=51.2MHz, MIPI_CLK=307MHz → lane bit rate=614Mbps
 *
 * 初始化顺序:
 *   DSI PHY LDO → DSI Bus → DBI IO → JD9165 Panel → Init → Backlight
 */

#include "lcd.h"

#include "freertos/FreeRTOS.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

/* JD9165 驱动组件 (components/esp_lcd_jd9165) */
#include "esp_lcd_jd9165.h"

/* esp_lv_adapter: 防撕裂模式与帧缓冲计算 */
#include "esp_lv_adapter.h"

static const char *TAG = "LCD";

/* ── 全局面板句柄 ── */
static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_panel_io = NULL;

/* ── 背光 GPIO 初始化 ── */
#if CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO >= 0
static void backlight_gpio_init(void)
{
    gpio_config_t bk_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO,
    };
    gpio_config(&bk_conf);
    gpio_set_level(CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO, 0);
}
#endif

/* ── MIPI DSI PHY 供电 ── */
static void dsi_phy_power_on(void)
{
    esp_ldo_channel_handle_t ldo_phy = NULL;
    esp_ldo_channel_config_t ldo_config = {
        .chan_id = 3,           /* LDO_VO3 → VDD_MIPI_DPHY */
        .voltage_mv = 2500,     /* 2.5V */
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_config, &ldo_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY powered on (LDO_VO3, 2.5V)");
}

esp_err_t lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing JD9165BA MIPI DSI LCD (%dx%d)", LCD_WIDTH, LCD_HEIGHT);

    /* 0. 背光初始化（先关闭） */
#if CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO >= 0
    backlight_gpio_init();
    gpio_set_level(CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO, 0);
#endif

    /* 1. MIPI DSI PHY 供电 */
    dsi_phy_power_on();

    /* 2. 创建 MIPI DSI 总线 */
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = CONFIG_SMARTBOX_MIPI_DSI_LANE_NUM,
        .lane_bit_rate_mbps = CONFIG_SMARTBOX_MIPI_DSI_LANE_BITRATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus));
    ESP_LOGI(TAG, "MIPI DSI bus created: %d lanes @ %ld Mbps",
             CONFIG_SMARTBOX_MIPI_DSI_LANE_NUM,
             (long)CONFIG_SMARTBOX_MIPI_DSI_LANE_BITRATE_MBPS);

    /* 3. 创建 DBI IO（用于发送 LCD 命令和参数） */
    esp_lcd_panel_io_handle_t dbi_io = NULL;
    esp_lcd_dbi_io_config_t dbi_config = JD9165BA_PANEL_IO_DBI_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &dbi_io));
    s_panel_io = dbi_io;
    ESP_LOGI(TAG, "DBI IO created");

    /* 4. 从 Kconfig 读取防撕裂模式 + 旋转角度，调用 esp_lvgl_adapter 计算 num_fbs
     *
     * 对应文档推荐用法:
     *   uint8_t num_fbs = esp_lv_adapter_get_required_frame_buffer_count(
     *       ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI,
     *       ESP_LV_ADAPTER_ROTATE_0);
     */
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

    uint8_t num_fbs = esp_lv_adapter_get_required_frame_buffer_count(tear_mode, rotation);
    ESP_LOGI(TAG, "esp_lv_adapter: tear_mode=%d, rotation=%d → num_fbs=%d",
             (int)tear_mode, (int)rotation, (int)num_fbs);

    esp_lcd_dpi_panel_config_t dpi_config = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 51,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888,
        .num_fbs = num_fbs,
        .video_timing = {
            .h_size = 1024,
            .v_size = 600,
            .hsync_back_porch = 136,
            .hsync_pulse_width = 24,
            .hsync_front_porch = 160,
            .vsync_back_porch = 21,
            .vsync_pulse_width = 2,
            .vsync_front_porch = 12,
        },
        .flags.use_dma2d = CONFIG_SMARTBOX_DMA2D_ENABLE,
    };

    jd9165_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_config,
        },
        .init_cmds = NULL,          /* NULL → 使用 esp_lcd_jd9165 内置默认序列 */
        .init_cmds_size = 0,
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_SMARTBOX_LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,   /* LVGL RGB888 stores {B,G,R} in memory */
        .bits_per_pixel = 24,       /* RGB888 */
        .vendor_config = &vendor_config,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(dbi_io, &panel_config, &s_panel));
    ESP_LOGI(TAG, "JD9165 panel created");

    /* 5. 初始化序列 (不单独调用 reset — init 内部已处理全部初始化命令) */
    esp_task_wdt_reset();  /* DSI 初始化命令序列耗时较长，手动喂狗 */
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    esp_task_wdt_reset();
    ESP_LOGI(TAG, "LCD panel initialized");

    /* 6. 开启背光 */
#if CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO >= 0
    gpio_set_level(CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO, 1);
    ESP_LOGI(TAG, "Backlight ON (GPIO %d)", CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO);
#else
    ESP_LOGI(TAG, "Backlight not controlled by GPIO (use external PWM)");
#endif

    ESP_LOGI(TAG, "LCD init complete");
    return ESP_OK;
}

esp_lcd_panel_handle_t lcd_get_panel(void)
{
    return s_panel;
}

esp_lcd_panel_io_handle_t lcd_get_panel_io(void)
{
    return s_panel_io;
}

esp_err_t lcd_set_backlight(uint8_t level)
{
#if CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO >= 0
    gpio_set_level(CONFIG_SMARTBOX_LCD_BACKLIGHT_GPIO, level ? 1 : 0);
#endif
    return ESP_OK;
}