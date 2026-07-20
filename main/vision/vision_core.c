/**
 * @file vision_core.c
 * @brief Vision 平台层实现
 *
 * 单后台线程驱动状态机:
 *   IDLE → PREPARE → CAPTURE → UPLOAD → PROCESS → DISPLAY → CLEANUP → IDLE
 *
 * 任何阶段检测 cancel_flag → CANCELLED → CLEANUP
 * 任何阶段超时 → ERROR → CLEANUP
 */
#include "vision_core.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "VISION_CORE";

#define STACK_LOG(phase) ESP_LOGI(TAG, "stack[%s] remain=%lu", \
    phase, (unsigned long)uxTaskGetStackHighWaterMark(NULL))

#define DEFAULT_CAPTURE_TIMEOUT_MS   30000
#define DEFAULT_UPLOAD_TIMEOUT_MS    60000
#define DEFAULT_PROCESS_TIMEOUT_MS   30000

/* ═══════════════════════════════════════════════
   内部状态
   ═══════════════════════════════════════════════ */
static TaskHandle_t     s_task       = NULL;
static QueueHandle_t    s_queue      = NULL;  /* 深度=1, 元素=任务配置拷贝 */
static volatile bool    s_cancel     = false;
static volatile vision_state_t s_state = VISION_IDLE;

/* ═══════════════════════════════════════════════
   队列元素 (cfg + cb + user_data)
   ═══════════════════════════════════════════════ */
typedef struct {
    vision_task_config_t cfg;
    vision_callback_t    cb;
    void                *user_data;
} vision_queue_item_t;

/* ═══════════════════════════════════════════════
   后台线程
   ═══════════════════════════════════════════════ */

static void _notify_state(const vision_callback_t *cb, void *user_data, vision_state_t state)
{
    s_state = state;
    if (cb && cb->on_state) {
        cb->on_state(state, user_data);
    }
}

static void _notify_error(const vision_callback_t *cb, void *user_data,
                          int code, const char *msg)
{
    ESP_LOGE(TAG, "Error 0x%04x: %s", code, msg);
    if (cb && cb->on_error) {
        cb->on_error(code, msg, user_data);
    }
}

static void _notify_result(const vision_callback_t *cb, void *user_data,
                           const vision_result_t *result)
{
    ESP_LOGI(TAG, "Result ready");
    if (cb && cb->on_result) {
        cb->on_result(result, user_data);
    }
}

#define WDT_FEED() esp_task_wdt_reset()

static void vision_task(void *arg)
{
    (void)arg;
    vision_queue_item_t  item;

    /* 注册到 TWDT (超时 30s, 每个阶段喂狗) */
    esp_task_wdt_add(NULL);
    STACK_LOG("boot");
    WDT_FEED();

    while (1) {
        /* 等待任务 — 用 1s 超时代替 portMAX_DELAY, 期间喂狗 */
        while (xQueueReceive(s_queue, &item, pdMS_TO_TICKS(1000)) != pdTRUE) {
            WDT_FEED();
        }

        vision_task_config_t *cfg       = &item.cfg;
        vision_callback_t    *cb        = &item.cb;
        void                 *user_data = item.user_data;

        /* vision_result_t 太大(6.4KB)不能放栈上 — heap 分配 */
        vision_result_t *result = calloc(1, sizeof(vision_result_t));
        if (!result) {
            ESP_LOGE(TAG, "Failed to alloc vision_result_t");
            continue;
        }

        ESP_LOGI(TAG, "=== Task [%s] started ===", cfg->name ? cfg->name : "?");
        s_cancel = false;
        STACK_LOG("start");
        WDT_FEED();
        int ret;

        /* ── PREPARE ── */
        _notify_state(cb, user_data, VISION_PREPARE);
        if (s_cancel) goto cancel;

        /* ── CAPTURE ── */
        STACK_LOG("pre-capture");
        WDT_FEED();
        _notify_state(cb, user_data, VISION_CAPTURE);
        if (cfg->fn_capture) {
            ESP_LOGI(TAG, "Capture phase");
            ret = cfg->fn_capture(user_data, result);
            if (ret != 0) {
                _notify_error(cb, user_data, ERR_VISION_CAPTURE_FAILED, "Capture failed");
                goto cleanup;
            }
        }
        STACK_LOG("post-capture");
        WDT_FEED();
        if (s_cancel) goto cancel;

        /* ── UPLOAD ── */
        STACK_LOG("pre-upload");
        WDT_FEED();
        _notify_state(cb, user_data, VISION_UPLOAD);
        if (cfg->fn_upload) {
            ESP_LOGI(TAG, "Upload phase");
            ret = cfg->fn_upload(user_data, result);
            if (ret != 0) {
                _notify_error(cb, user_data, ERR_VISION_UPLOAD_FAILED, "Upload failed");
                goto cleanup;
            }
        }
        STACK_LOG("post-upload");
        WDT_FEED();
        if (s_cancel) goto cancel;

        /* ── PROCESS ── */
        STACK_LOG("pre-process");
        WDT_FEED();
        _notify_state(cb, user_data, VISION_PROCESS);
        if (cfg->fn_process) {
            ESP_LOGI(TAG, "Process phase");
            ret = cfg->fn_process(user_data, result);
            if (ret != 0) {
                _notify_error(cb, user_data, ERR_VISION_PROCESS_FAILED, "Process failed");
                goto cleanup;
            }
        }
        STACK_LOG("post-process");
        WDT_FEED();
        if (s_cancel) goto cancel;

        /* ── DISPLAY ── */
        _notify_state(cb, user_data, VISION_DISPLAY);
        _notify_result(cb, user_data, result);
        goto cleanup;

cancel:
        ESP_LOGI(TAG, "Task cancelled");
        _notify_state(cb, user_data, VISION_CANCELLED);

cleanup:
        STACK_LOG("cleanup");
        WDT_FEED();
        _notify_state(cb, user_data, VISION_CLEANUP);
        if (cfg->fn_cleanup) {
            cfg->fn_cleanup(user_data, result);
        }
        ESP_LOGI(TAG, "=== Task [%s] finished ===", cfg->name ? cfg->name : "?");
        free(result);
    }
}

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

int vision_core_init(void)
{
    if (s_queue) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }

    /* 重新配置 TWDT 超时: HTTP 上传需要 30-60s (TLS ~4s + 上传 ~20s + 响应 ~10s) */
    esp_task_wdt_config_t twdt_cfg = {
        .timeout_ms     = 60000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic  = true,
    };
    esp_err_t wdt_ret = esp_task_wdt_reconfigure(&twdt_cfg);
    if (wdt_ret == ESP_OK) {
        ESP_LOGI(TAG, "TWDT timeout reconfigured to 60s");
    } else {
        /* 可能 TWDT 未初始化或 API 不支持 — 非致命 */
        ESP_LOGW(TAG, "TWDT reconfigure failed: %s (0x%x), "
                 "default timeout applies", esp_err_to_name(wdt_ret), wdt_ret);
    }

    s_queue = xQueueCreate(1, sizeof(vision_queue_item_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ERR_VISION_BUSY;
    }

    if (xTaskCreate(vision_task, "vision_task",
                    CONFIG_VISION_TASK_STACK, NULL,
                    CONFIG_VISION_TASK_PRIO, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ERR_VISION_BUSY;
    }

    ESP_LOGI(TAG, "Initialized (stack=%d, prio=%d)",
             CONFIG_VISION_TASK_STACK, CONFIG_VISION_TASK_PRIO);
    return 0;
}

void vision_core_deinit(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    s_state = VISION_IDLE;
    ESP_LOGI(TAG, "Deinit");
}

int vision_start(const vision_task_config_t *cfg,
                 const vision_callback_t *cb, void *user_data)
{
    if (!cfg) return ERR_VISION_BUSY;
    if (vision_is_busy()) {
        ESP_LOGW(TAG, "Busy — reject [%s]", cfg->name ? cfg->name : "?");
        return ERR_VISION_BUSY;
    }

    vision_queue_item_t item;
    memset(&item, 0, sizeof(item));
    item.cfg       = *cfg;
    item.cb        = *cb;
    item.user_data = user_data;

    if (xQueueSend(s_queue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Queue full");
        return ERR_VISION_BUSY;
    }

    ESP_LOGI(TAG, "Task [%s] queued", cfg->name ? cfg->name : "?");
    return 0;
}

int vision_cancel(void)
{
    if (!vision_is_busy()) return 0;
    s_cancel = true;
    ESP_LOGI(TAG, "Cancel requested");
    return 0;
}

bool vision_is_busy(void)
{
    return (s_state != VISION_IDLE && s_state != VISION_CLEANUP);
}

vision_state_t vision_get_state(void)
{
    return s_state;
}
