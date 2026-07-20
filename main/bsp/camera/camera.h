/**
 * @file camera.h
 * @brief Camera BSP — OV5647 MIPI CSI + ISP 驱动
 *
 * 硬件: OV5647 MIPI CSI (2-lane), ESP32-P4 MIPI CSI host
 * I2C:  共享触摸屏 GPIO7/8 总线 (通过 touch_get_i2c_bus())
 * 参考: E:\esp32-tool\examples\peripherals\camera\mipi_isp_dsi
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 OV5647 摄像头 (MIPI CSI + ISP + 传感器)
 *
 * 初始化序列:
 *  1. MIPI PHY LDO 供电
 *  2. 获取共享 I2C 总线 (touch_get_i2c_bus)
 *  3. SCCB 初始化 + 传感器自动检测
 *  4. 传感器格式配置 (RAW8, 1024x600, 30fps)
 *  5. MIPI CSI 控制器配置
 *  6. ISP 处理器配置 (RAW8→RGB565)
 *  7. 启动 CSI 流
 *
 * @return ESP_OK 成功
 */
esp_err_t camera_init(void);

/**
 * @brief 反初始化摄像头
 * @return ESP_OK 成功
 */
esp_err_t camera_deinit(void);

/**
 * @brief 捕获一帧图像 (RGB565 格式)
 *
 * 捕获 ISP 输出的 RGB565 帧，通过 CSI DMA 接收。
 * 输出为 RGB565 格式，调用方负责 free(*data)。
 *
 * @param[out] data 图像数据指针 (PSRAM 分配，调用方负责释放)
 * @param[out] len  数据长度 (字节)
 * @return ESP_OK 成功
 */
esp_err_t camera_capture(uint8_t **data, size_t *len);

/**
 * @brief RGB565 → JPEG 硬件编码 (ESP32-P4 esp_jpeg)
 *
 * 使用 ESP32-P4 硬件 JPEG 编码器将 RGB565 原始帧转换为 JPEG。
 * 用于 DeepSeek-VL2 等需要 JPEG 输入的多模态 API。
 *
 * @param[in]  rgb565      RGB565 原始帧数据
 * @param[in]  rgb565_len  帧长度 (字节)
 * @param[out] jpeg_out    JPEG 输出 (PSRAM 分配，调用方负责释放)
 * @param[out] jpeg_len    JPEG 长度 (字节)
 * @return ESP_OK 成功
 */
esp_err_t camera_rgb565_to_jpeg(const uint8_t *rgb565, size_t rgb565_len,
                                 uint8_t **jpeg_out, size_t *jpeg_len);

/**
 * @brief 获取图像宽度 (像素)
 * @return 图像宽度
 */
uint16_t camera_get_width(void);

/**
 * @brief 获取图像高度 (像素)
 * @return 图像高度
 */
uint16_t camera_get_height(void);

/**
 * @brief 摄像头是否已连接/初始化
 * @return true 已连接
 */
bool camera_is_connected(void);

/**
 * @brief 启动连续采集模式（预览用，不停止 CSI 流）
 *
 * 启动后摄像头持续输出帧到内部缓冲，
 * 通过 camera_get_latest_frame() 获取最新帧拷贝。
 *
 * @return ESP_OK 成功
 */
esp_err_t camera_start_streaming(void);

/**
 * @brief 停止连续采集模式
 * @return ESP_OK 成功
 */
esp_err_t camera_stop_streaming(void);

/**
 * @brief 获取最新一帧的拷贝（连续采集模式下使用）
 *
 * 从内部缓冲拷贝最新帧数据到输出指针。
 * 不停止 CSI 流，适合实时预览。
 *
 * @param[out] data 图像数据指针 (PSRAM 分配，调用方负责释放)
 * @param[out] len  数据长度 (字节)
 * @return ESP_OK 成功
 */
esp_err_t camera_get_latest_frame(uint8_t **data, size_t *len);

/**
 * @brief 是否处于连续采集模式
 * @return true 正在连续采集
 */
bool camera_is_streaming(void);

#ifdef __cplusplus
}
#endif