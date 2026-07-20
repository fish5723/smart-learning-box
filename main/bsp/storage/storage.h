/**
 * @file storage.h
 * @brief 统一存储模块 — 基于 NVS 的键值持久化
 *
 * 所有配置/数据必须通过本模块访问 NVS，禁止各模块自行创建 NVS Namespace。
 * (AGENTS.md §12 配置管理规范)
 *
 * 提供:
 *   - storage_init()              初始化 NVS 分区
 *   - storage_get_str / set_str   通用字符串读写
 *   - storage_erase_key           删除键
 *   - storage_save_wifi_cred / load_wifi_cred / erase_wifi_cred  WiFi 凭证便捷接口
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── NVS 配置 ── */
#define STORAGE_NVS_NAMESPACE   "storage"
#define STORAGE_MAX_STR_LEN     128

/* ── 通用 API ── */

/**
 * @brief 初始化存储模块（打开 NVS 命名空间）
 *
 * 调用 nvs_flash_init() + nvs_open()。
 * 需在系统初始化阶段调用一次，晚于 NVS flash 初始化。
 *
 * @return ESP_OK 成功
 */
esp_err_t storage_init(void);

/**
 * @brief 读取字符串配置
 *
 * @param key       键名
 * @param[out] buf  输出缓冲区
 * @param[in,out] len  输入: 缓冲区大小; 输出: 实际字符串长度
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 键不存在
 */
esp_err_t storage_get_str(const char *key, char *buf, size_t *len);

/**
 * @brief 写入字符串配置
 *
 * @param key  键名
 * @param val  字符串值
 * @return ESP_OK 成功
 */
esp_err_t storage_set_str(const char *key, const char *val);

/**
 * @brief 删除键
 *
 * @param key  键名
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 键不存在（幂等）
 */
esp_err_t storage_erase_key(const char *key);

/**
 * @brief 获取布尔值
 *
 * @param key       键名
 * @param[out] val  输出
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 键不存在
 */
esp_err_t storage_get_bool(const char *key, bool *val);

/**
 * @brief 写入布尔值（内部存储为 uint8_t）
 *
 * @param key  键名
 * @param val  布尔值
 * @return ESP_OK 成功
 */
esp_err_t storage_set_bool(const char *key, bool val);

esp_err_t storage_get_u32(const char *key, uint32_t *val);
esp_err_t storage_set_u32(const char *key, uint32_t val);

/* ── WiFi 凭证便捷接口 ── */

/**
 * @brief 保存 WiFi 凭证 (SSID + 密码) 到 NVS
 *
 * @param ssid  WiFi SSID (最大 32 字节)
 * @param pwd   WiFi 密码 (最大 63 字节, NULL 表示开放网络)
 * @return ESP_OK 成功
 */
esp_err_t storage_save_wifi_cred(const char *ssid, const char *pwd);

/**
 * @brief 从 NVS 加载 WiFi 凭证
 *
 * @param[out] ssid_buf   SSID 输出缓冲区
 * @param      ssid_len   SSID 缓冲区大小
 * @param[out] pwd_buf    密码输出缓冲区
 * @param      pwd_len    密码缓冲区大小
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 未保存过凭证
 */
esp_err_t storage_load_wifi_cred(char *ssid_buf, size_t ssid_len,
                                  char *pwd_buf, size_t pwd_len);

/**
 * @brief 擦除已保存的 WiFi 凭证
 *
 * @return ESP_OK 成功（含键不存在的情况）
 */
esp_err_t storage_erase_wifi_cred(void);

/* ── LLM 凭证便捷接口 ── */

/**
 * @brief 保存 LLM API Key 到 NVS
 *
 * @param api_key  LLM API Key（Access Token）
 * @return ESP_OK 成功
 */
esp_err_t storage_save_llm_key(const char *api_key);

/**
 * @brief 从 NVS 加载 LLM API Key
 *
 * @param[out] buf  输出缓冲区
 * @param[in,out] len  输入: 缓冲区大小; 输出: 实际长度
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 未配置
 */
esp_err_t storage_load_llm_key(char *buf, size_t *len);

/**
 * @brief 擦除已保存的 LLM API Key
 *
 * @return ESP_OK 成功
 */
esp_err_t storage_erase_llm_key(void);

/* ── Qwen (通义千问) 凭证便捷接口 ── */

esp_err_t storage_save_qwen_key(const char *api_key);
esp_err_t storage_load_qwen_key(char *buf, size_t *len);
esp_err_t storage_erase_qwen_key(void);

#ifdef __cplusplus
}
#endif