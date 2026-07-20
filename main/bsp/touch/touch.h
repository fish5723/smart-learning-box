/**
 * @file touch.h
 * @brief Touch BSP 模块 — GT911 电容触摸 (I2C)
 *
 * 驱动: espressif/esp_lcd_touch_gt911 (官方组件 v2.0+)
 * 触摸芯片: GT911 (汇顶)
 * 接口: I2C, 7-bit addr 0x14
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOUCH_MAX_POINTS        5       /**< GT911 最大支持触摸点数 */

/* ── 触摸点数据 ── */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t strength;      /**< 触摸力度/面积 */
    uint8_t  track_id;      /**< 触摸点跟踪 ID */
    uint8_t  pressed;       /**< 0=释放, 1=按下 */
} touch_point_t;

/* ── 多点触摸数据 ── */
typedef struct {
    uint8_t         num;                    /**< 当前触摸点数 */
    touch_point_t   points[TOUCH_MAX_POINTS]; /**< 触摸点数组 */
} touch_points_t;

/**
 * @brief 初始化 GT911 触摸控制器
 *
 * 流程:
 *  1. 创建 I2C master 总线
 *  2. 创建 I2C panel IO (ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG)
 *  3. 调用 esp_lcd_touch_new_i2c_gt911() 执行复位 + 初始化
 *
 * @return ESP_OK 成功，否则失败
 */
esp_err_t touch_init(void);

/**
 * @brief 反初始化 GT911 触摸控制器
 *
 * 释放所有分配的资源。
 *
 * @return ESP_OK 成功
 */
esp_err_t touch_deinit(void);

/**
 * @brief 获取 esp_lcd_touch_handle_t
 *
 * 必须在 touch_init() 之后调用。
 *
 * @return 触摸句柄，未初始化时返回 NULL
 */
esp_lcd_touch_handle_t touch_get_handle(void);

/**
 * @brief 读取单个触摸点数据（兼容旧 API）
 *
 * @param[out] point 触摸点
 * @return ESP_OK 有触摸数据，ESP_ERR_NOT_FOUND 无触摸
 */
esp_err_t touch_read(touch_point_t *point);

/**
 * @brief 读取多点触摸数据
 *
 * @param[out] points 多点触摸数据
 * @return ESP_OK 有触摸数据，ESP_ERR_NOT_FOUND 无触摸
 */
esp_err_t touch_read_multi(touch_points_t *points);

/**
 * @brief 获取共享 I2C 总线句柄（供摄像头 SCCB 使用）
 *
 * 摄像头 OV5647 与触摸屏 GT911 共用同一物理 I2C 总线 (GPIO7/8)，
 * 此接口允许摄像头模块复用触摸屏已创建的 I2C 总线。
 *
 * @return I2C 总线句柄，未初始化时返回 NULL
 */
i2c_master_bus_handle_t touch_get_i2c_bus(void);

#ifdef __cplusplus
}
#endif
