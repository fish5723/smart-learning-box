/**
 * @file sd_card.h
 * @brief SD 卡 BSP 驱动 — SDMMC 4-bit + FatFS 挂载
 *
 * 引脚映射 (ESP32-P4 SDMMC Slot 0, IOMUX 固定):
 *   CLK=43, CMD=44, D0=39, D1=40, D2=41, D3=42
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 SD 卡 — SPI 初始化 + FatFS 挂载
 *
 * 上电时序: SPI 总线 → 卡检测 → 总线配置 → FAT 挂载。
 * 首次使用或文件系统损坏时自动格式化 (format_if_mount_failed=true)。
 * 必须晚于 nvs_flash 初始化调用。
 *
 * @return ESP_OK 挂载成功; ESP_FAIL / ESP_ERR_NOT_FOUND 卡未插入或硬件错误
 */
esp_err_t sd_card_init(void);

/**
 * @brief 卸载 SD 卡并释放 SDMMC 外设
 *
 * 使用场景: OTA 固件升级前、深度睡眠前、安全弹出。
 * 卸载后 POSIX 文件操作不再可用。
 *
 * @return ESP_OK 卸载成功
 */
esp_err_t sd_card_deinit(void);

/**
 * @brief 查询 SD 卡是否已挂载
 *
 * @return true  已挂载, POSIX 文件操作可用
 * @return false 未挂载或已卸载
 */
bool sd_card_is_mounted(void);

/**
 * @brief 获取 SD 卡挂载点路径
 *
 * @return 挂载点字符串, 如 "/sdcard"。
 *         未挂载时仍返回常量字符串, 调用方应先检查 sd_card_is_mounted()。
 */
const char *sd_card_get_mount_point(void);

/**
 * @brief 获取 SD 卡总容量 (MB)
 *
 * @param[out] total_mb  总容量 (MB)
 * @param[out] free_mb   可用容量 (MB), 可为 NULL
 * @return ESP_OK 成功; ESP_ERR_INVALID_STATE 未挂载
 */
esp_err_t sd_card_get_capacity(int *total_mb, int *free_mb);

/**
 * @brief SD 卡独立测试 — 挂载 + 读写验证 + 信息打印
 *
 * 仅依赖 nvs_flash + SDMMC, 不依赖 LCD/LVGL/WiFi。
 * 用于首次调试 SD 卡硬件连接。
 */
void sd_card_run_test(void);

#ifdef __cplusplus
}
#endif