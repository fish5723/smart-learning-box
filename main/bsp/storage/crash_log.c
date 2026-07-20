/**
 * @file crash_log.c
 * @brief 崩溃日志实现 — 复位原因 + 系统状态快照，通过串口 ESP_LOG 输出
 *
 * 设计原则:
 *   - 不写 SD 卡 — 避免频繁复位时损坏 SD 卡 (boot loop 风险)
 *   - 所有输出通过 ESP_LOGI/ESP_LOGE 走串口
 *   - 保持 crash_log_init / crash_log_dump_state API 签名不变
 */

#include "crash_log.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CRASH_LOG";

/* ── 复位原因转换表 ── */
static const char *reset_reason_str(int reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN:    return "UNKNOWN";
    case ESP_RST_POWERON:    return "POWERON";
    case ESP_RST_EXT:        return "EXT_PIN";
    case ESP_RST_SW:         return "SW_RESET";
    case ESP_RST_PANIC:      return "PANIC";
    case ESP_RST_INT_WDT:    return "INT_WDT";
    case ESP_RST_TASK_WDT:   return "TASK_WDT";
    case ESP_RST_WDT:        return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:   return "BROWNOUT";
    case ESP_RST_SDIO:       return "SDIO";
    default:                 return "UNKNOWN_CODE";
    }
}

/* ── 判断是否为异常复位 ── */
static bool is_abnormal_reset(int reason)
{
    return (reason != ESP_RST_UNKNOWN &&
            reason != ESP_RST_POWERON &&
            reason != ESP_RST_EXT &&
            reason != ESP_RST_SW &&
            reason != ESP_RST_DEEPSLEEP);
}

/* ── 内部: 通过串口输出系统状态 dump ── */
static void log_state_dump(const char *trigger_reason)
{
    ESP_LOGI(TAG, "===== Crash State Dump =====");
    ESP_LOGI(TAG, "Trigger:    %s", trigger_reason);
    ESP_LOGI(TAG, "Timestamp:  %lld ms", esp_timer_get_time() / 1000);

    /* 复位原因 */
    int reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset Rsn:  %s (%d)", reset_reason_str(reason), reason);

    /* 堆内存 */
    ESP_LOGI(TAG, "Free Heap:  %lu bytes",
            (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min Free:   %lu bytes",
            (unsigned long)esp_get_minimum_free_heap_size());

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    /* 任务列表 */
    ESP_LOGI(TAG, "----- Task List -----");
    char task_buf[2048];
#if CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
    vTaskList(task_buf);
    ESP_LOGI(TAG, "\n%s", task_buf);
#else
    ESP_LOGI(TAG, "(enable USE_STATS_FORMATTING_FUNCTIONS for task list)");
#endif
#endif

    ESP_LOGI(TAG, "===== End of Dump =====");
}

esp_err_t crash_log_init(void)
{
    int reason = esp_reset_reason();
    ESP_LOGI(TAG, "Boot reset reason: %s (%d)", reset_reason_str(reason), reason);

    if (is_abnormal_reset(reason)) {
        /* 异常复位 → 通过串口输出状态快照，不写 SD 卡 */
        ESP_LOGW(TAG, "Abnormal reset detected, dumping state to serial...");
        log_state_dump(reset_reason_str(reason));
    }

    ESP_LOGI(TAG, "Crash log initialized (serial-only mode)");
    return ESP_OK;
}

esp_err_t crash_log_dump_state(const char *trigger_reason)
{
    ESP_LOGI(TAG, "Manual state dump requested: %s", trigger_reason ? trigger_reason : "unknown");
    log_state_dump(trigger_reason ? trigger_reason : "unknown");
    return ESP_OK;
}
