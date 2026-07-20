/**
 * @brief LCD BSP 模块 — JD9165BA MIPI DSI 驱动
 *
 * 驱动芯片: JD9165BA
 * 分辨率:   1024×600 (可从 Kconfig 配置)
 * 接口:     MIPI DSI, 2-lane, 600Mbps
 * 参考:     peripherals/lcd/mipi_dsi 官方示例
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 分辨率（从 Kconfig 读取，默认 1024×600） ── */
#define LCD_WIDTH   CONFIG_SMARTBOX_LCD_H_RES
#define LCD_HEIGHT  CONFIG_SMARTBOX_LCD_V_RES

/**
 * @brief 初始化 MIPI DSI 总线 + JD9165BA LCD 面板
 *
 * 执行流程:
 *  1. 使能 MIPI DSI PHY 电源 (LDO)
 *  2. 创建 DSI 总线 (esp_lcd_new_dsi_bus)
 *  3. 创建 DBI IO    (esp_lcd_new_panel_io_dbi)
 *  4. 创建 JD9165 面板 (esp_lcd_new_panel_jd9165)
 *  5. 硬件复位 + 初始化序列
 *
 * @return ESP_OK 成功，否则失败
 */
esp_err_t lcd_init(void);

/**
 * @brief 获取 LCD 面板句柄（供 LVGL 绑定用）
 * @return 面板句柄，未初始化时返回 NULL
 */
esp_lcd_panel_handle_t lcd_get_panel(void);

/**
 * @brief 获取 MIPI DSI DBI IO 句柄（供 esp_lv_adapter 使用）
 * @return IO 句柄，未初始化时返回 NULL
 */
esp_lcd_panel_io_handle_t lcd_get_panel_io(void);

/**
 * @brief 设置背光亮度
 * @param level 0=关闭, 1=开启（后续可改为 PWM 占空比 0~100）
 * @return ESP_OK
 */
esp_err_t lcd_set_backlight(uint8_t level);

#ifdef __cplusplus
}
#endif
