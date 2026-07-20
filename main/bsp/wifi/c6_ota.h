/**
 * @file c6_ota.h
 * @brief C6 协处理器固件 OTA — 从 P4 Flash 分区通过 SDIO 更新 C6
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 检查并执行 C6 固件 OTA
 *
 * 从 P4 Flash 的 "slave_fw" 分区读取固件, 与 C6 当前运行版本比较,
 * 若不同则通过 SDIO 推送到 C6。成功后 P4 自动重启。
 *
 * 必须在 esp_hosted 已初始化 (esp_wifi_init 之后) 时调用。
 *
 * @return ESP_OK                      无需更新或更新成功(函数返回前已重启)
 *         ESP_ERR_NOT_FOUND            slave_fw 分区不存在或为空
 *         ESP_ERR_INVALID_STATE        SDIO/Hosted 未初始化
 *         other                        更新失败
 */
esp_err_t c6_ota_check_and_update(void);

#ifdef __cplusplus
}
#endif
