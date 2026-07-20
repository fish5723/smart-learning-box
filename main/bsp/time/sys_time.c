/**
 * @file time.c
 * @brief 系统时间模块 — SNTP 网络时间同步实现
 *
 * 使用 lwIP SNTP 模块 (sdkconfig 已启用) 从 NTP 服务器同步系统时间。
 * WiFi 获取 IP 后自动启动同步，断开后停止。
 * 时区固定为中国标准时间 (CST, UTC+8)。
 */

#include "sys_time.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include <string.h>
#include <time.h>

static const char *TAG = "TIME";

/* ── 内部状态 ── */
static bool              s_initialized   = false;
static bool              s_running       = false;
static time_sync_state_t s_sync_state    = TIME_SYNC_IDLE;

/* ═══════════════════════════════════════════════
   SNTP 同步通知回调
   ═══════════════════════════════════════════════ */
static void sntp_notification_cb(struct timeval *tv)
{
    if (tv) {
        s_sync_state = TIME_SYNC_COMPLETED;

        /* 获取本地时间用于日志 */
        struct tm timeinfo = {0};
        time_t now = tv->tv_sec;
        localtime_r(&now, &timeinfo);

        ESP_LOGI(TAG, "Time synchronized: %04d-%02d-%02d %02d:%02d:%02d UTC+8",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        s_sync_state = TIME_SYNC_FAILED;
        ESP_LOGW(TAG, "SNTP sync notification with NULL timeval — sync failed");
    }
}

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

esp_err_t time_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing time module...");

    /* 1. 设置时区: CST (UTC+8) */
    setenv("TZ", TIME_TZ_CST, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to %s (UTC+8)", TIME_TZ_CST);

    /* 2. 配置 SNTP */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, TIME_NTP_SERVER);
    esp_sntp_setservername(1, TIME_NTP_SERVER_BACKUP);
    esp_sntp_set_time_sync_notification_cb(sntp_notification_cb);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();

    ESP_LOGI(TAG, "NTP server: %s (backup: %s)", TIME_NTP_SERVER, TIME_NTP_SERVER_BACKUP);

    s_initialized = true;
    ESP_LOGI(TAG, "Time module initialized");
    return ESP_OK;
}

esp_err_t time_sync_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized — call time_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_running) {
        ESP_LOGW(TAG, "SNTP already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting SNTP sync...");

    /* 重启 SNTP 以触发新的同步请求 */
    esp_sntp_restart();

    s_running = true;
    s_sync_state = TIME_SYNC_IN_PROGRESS;
    ESP_LOGI(TAG, "SNTP sync started (will retry automatically)");
    return ESP_OK;
}

void time_sync_stop(void)
{
    if (!s_running) return;

    ESP_LOGI(TAG, "Stopping SNTP sync...");
    esp_sntp_stop();
    s_running = false;
    s_sync_state = TIME_SYNC_IDLE;
    ESP_LOGI(TAG, "SNTP sync stopped");
}

bool time_is_synchronized(void)
{
    if (!s_running) return false;

    /* sntp_get_sync_status() 返回 SNTP_SYNC_STATUS_COMPLETED (1)
       表示最近一次同步已成功完成 */
    sntp_sync_status_t status = sntp_get_sync_status();
    if (status == SNTP_SYNC_STATUS_COMPLETED) {
        s_sync_state = TIME_SYNC_COMPLETED;
        return true;
    }
    return false;
}

time_sync_state_t time_get_sync_state(void)
{
    /* 如果 SNTP 报告已完成但内部状态未更新，修正之 */
    if (s_running && s_sync_state != TIME_SYNC_COMPLETED) {
        if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            s_sync_state = TIME_SYNC_COMPLETED;
        }
    }
    return s_sync_state;
}

time_t time_get_now(void)
{
    return time(NULL);
}

const char *time_get_local_str(char *buf, size_t len)
{
    if (!buf || len == 0) return NULL;

    time_t now = time(NULL);
    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);

    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    return buf;
}