/**
 * @file storage.c
 * @brief 统一存储模块 — NVS 持久化实现
 *
 * 参考: ESP-IDF nvs_flash 标准 API
 * 官方 esp_wifi_remote 从机侧通过 CONFIG_ESP_HOSTED_WIFI_NVS_ENABLED 启用 NVS
 * (slave_wifi_std.c L478-485)，主机侧在此独立管理用户级凭证。
 */

#include "storage.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "STORAGE";

/* NVS 句柄 — 初始化后一直持有 */
static nvs_handle_t s_nvs_handle = 0;
static bool s_initialized = false;

/* WiFi 凭证键名 */
#define KEY_WIFI_SSID  "wifi_ssid"
#define KEY_WIFI_PWD   "wifi_pwd"

/* LLM 凭证键名 */
#define KEY_LLM_API_KEY  "llm_api_key"
#define KEY_QWEN_API_KEY "qwen_api_key"


/* ═══════════════════════════════════════════════
   通用 API
   ═══════════════════════════════════════════════ */

esp_err_t storage_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* NVS flash 由 system_init.c 层统一初始化，此处只需 open */
    esp_err_t ret = nvs_open(STORAGE_NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(%s) failed: %s", STORAGE_NVS_NAMESPACE, esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Storage initialized (ns=%s)", STORAGE_NVS_NAMESPACE);
    return ESP_OK;
}

esp_err_t storage_get_str(const char *key, char *buf, size_t *len)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!key || !buf || !len || *len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_get_str(s_nvs_handle, key, buf, len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "Key '%s' not found", key);
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str(%s) failed: %s", key, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t storage_set_str(const char *key, const char *val)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!key || !val) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_set_str(s_nvs_handle, key, val);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str(%s) failed: %s", key, esp_err_to_name(ret));
        return ret;
    }

    /* 立即提交确保持久化 */
    ret = nvs_commit(s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit() failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t storage_erase_key(const char *key)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_erase_key(s_nvs_handle, key);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;  /* 键不存在视为成功（幂等） */
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_erase_key(%s) failed: %s", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit() after erase failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t storage_get_bool(const char *key, bool *val)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!key || !val) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tmp = 0;
    esp_err_t ret = nvs_get_u8(s_nvs_handle, key, &tmp);
    if (ret == ESP_OK) {
        *val = (tmp != 0);
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "Key '%s' not found", key);
    }
    return ret;
}

esp_err_t storage_set_bool(const char *key, bool val)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_set_u8(s_nvs_handle, key, val ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u8(%s) failed: %s", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit() after set_bool failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t storage_get_u32(const char *key, uint32_t *val)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!key || !val) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_get_u32(s_nvs_handle, key, val);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "Key '%s' not found", key);
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_u32(%s) failed: %s", key, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t storage_set_u32(const char *key, uint32_t val)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_set_u32(s_nvs_handle, key, val);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_u32(%s) failed: %s", key, esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_commit(s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit() after set_u32 failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ═══════════════════════════════════════════════
   WiFi 凭证便捷接口
   ═══════════════════════════════════════════════ */

esp_err_t storage_save_wifi_cred(const char *ssid, const char *pwd)
{
    if (!ssid || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = storage_set_str(KEY_WIFI_SSID, ssid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save WiFi SSID failed");
        return ret;
    }

    if (pwd && strlen(pwd) > 0) {
        ret = storage_set_str(KEY_WIFI_PWD, pwd);
    } else {
        /* 开放网络：删除旧密码 */
        ret = storage_erase_key(KEY_WIFI_PWD);
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved: SSID=\"%s\"", ssid);
    }
    return ret;
}

esp_err_t storage_load_wifi_cred(char *ssid_buf, size_t ssid_len,
                                  char *pwd_buf, size_t pwd_len)
{
    if (!ssid_buf || ssid_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 先读取 SSID */
    size_t len = ssid_len;
    esp_err_t ret = storage_get_str(KEY_WIFI_SSID, ssid_buf, &len);
    if (ret != ESP_OK) {
        return ret;  /* SSID 不存在 → 整体视为无凭证 */
    }

    /* 再读取密码（可选） */
    if (pwd_buf && pwd_len > 0) {
        size_t pwd_len_io = pwd_len;
        ret = storage_get_str(KEY_WIFI_PWD, pwd_buf, &pwd_len_io);
        if (ret == ESP_ERR_NOT_FOUND) {
            /* 密码不存在 → 可能是开放网络 */
            pwd_buf[0] = '\0';
            ret = ESP_OK;
        }
    }

    ESP_LOGI(TAG, "WiFi credentials loaded: SSID=\"%s\"", ssid_buf);
    return ESP_OK;
}

esp_err_t storage_erase_wifi_cred(void)
{
    esp_err_t r1 = storage_erase_key(KEY_WIFI_SSID);
    esp_err_t r2 = storage_erase_key(KEY_WIFI_PWD);
    ESP_LOGI(TAG, "WiFi credentials erased");
    return (r1 != ESP_OK) ? r1 : r2;
}

/* ═══════════════════════════════════════════════
   LLM 凭证便捷接口
   ═══════════════════════════════════════════════ */

esp_err_t storage_save_llm_key(const char *api_key)
{
    if (!api_key || strlen(api_key) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = storage_set_str(KEY_LLM_API_KEY, api_key);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LLM API Key saved");
    }
    return ret;
}

esp_err_t storage_load_llm_key(char *buf, size_t *len)
{
    if (!buf || !len || *len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = storage_get_str(KEY_LLM_API_KEY, buf, len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LLM API Key loaded");
    }
    return ret;
}

esp_err_t storage_erase_llm_key(void)
{
    esp_err_t ret = storage_erase_key(KEY_LLM_API_KEY);
    ESP_LOGI(TAG, "LLM API Key erased");
    return ret;
}

/* ═══════════════════════════════════════════════
   Qwen (通义千问) 凭证便捷接口
   ═══════════════════════════════════════════════ */

esp_err_t storage_save_qwen_key(const char *api_key)
{
    if (!api_key || strlen(api_key) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = storage_set_str(KEY_QWEN_API_KEY, api_key);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Qwen API Key saved");
    }
    return ret;
}

esp_err_t storage_load_qwen_key(char *buf, size_t *len)
{
    if (!buf || !len || *len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = storage_get_str(KEY_QWEN_API_KEY, buf, len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Qwen API Key loaded");
    }
    return ret;
}

esp_err_t storage_erase_qwen_key(void)
{
    esp_err_t ret = storage_erase_key(KEY_QWEN_API_KEY);
    ESP_LOGI(TAG, "Qwen API Key erased");
    return ret;
}