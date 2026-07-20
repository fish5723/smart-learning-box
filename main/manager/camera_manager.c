/**
 * @file camera_manager.c
 * @brief 相机管理层实现 — 唯一 Camera Owner
 *
 * 两种工作模式:
 *   Preview 模式: 持续 CSI 流 → callback 投递帧 (OCR UI 实时预览)
 *   Capture 模式: 停止/重启 CSI 流 → 单帧捕获 (向后兼容)
 *
 * Preview 状态下 Capture 不可用 (camera_capture 需要停止 CSI 流,
 * 但 preview 依赖持续流).
 */
#include "camera_manager.h"
#include "bsp/camera/camera.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "CAM_MGR";

static SemaphoreHandle_t s_mutex = NULL;

/* ═══════════════════════════════════════════════
   Preview 模式状态
   ═══════════════════════════════════════════════ */
#define PREVIEW_TASK_STACK      4096
#define PREVIEW_TASK_PRIO       5
#define PREVIEW_PERIOD_MS       33    /* ~30 fps */

static TaskHandle_t         s_preview_task    = NULL;
static camera_preview_cb_t  s_preview_cb      = NULL;
static void                *s_preview_ctx     = NULL;
static vision_blob_t        s_frozen_frame    = {0};
static vision_blob_t        s_preview_frame   = {0};
static volatile bool        s_frozen          = false;
static volatile bool        s_preview_running = false;
static volatile bool        s_fps_reset       = false;  /* resume 后重置帧率计数 */
static SemaphoreHandle_t    s_freeze_mutex    = NULL;  /* 保护 frozen_frame 读写 */

/* ═══════════════════════════════════════════════
   Init / Deinit
   ═══════════════════════════════════════════════ */

int camera_manager_init(void)
{
    if (s_mutex) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create capture mutex");
        return ERR_CAMERA_NOT_READY;
    }

    s_freeze_mutex = xSemaphoreCreateMutex();
    if (!s_freeze_mutex) {
        ESP_LOGE(TAG, "Failed to create freeze mutex");
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ERR_CAMERA_NOT_READY;
    }

    if (!camera_is_connected()) {
        ESP_LOGW(TAG, "Camera not connected (init skipped or failed at BSP level)");
    }

    ESP_LOGI(TAG, "Ready (capture_mutex=%p, freeze_mutex=%p)",
             (void*)s_mutex, (void*)s_freeze_mutex);
    return 0;
}

void camera_manager_deinit(void)
{
    camera_manager_preview_stop();

    if (s_freeze_mutex) {
        vSemaphoreDelete(s_freeze_mutex);
        s_freeze_mutex = NULL;
    }
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    ESP_LOGI(TAG, "Deinit");
}

bool camera_manager_is_ready(void)
{
    return camera_is_connected();
}

/* ═══════════════════════════════════════════════
   Capture — RGB565 原始帧
   ═══════════════════════════════════════════════ */

int camera_manager_capture(vision_blob_t *rgb565)
{
    if (!rgb565) return ERR_CAMERA_NOT_READY;
    if (!camera_is_connected()) return ERR_CAMERA_NOT_READY;

    ESP_LOGI(TAG, "capture: waiting for mutex...");
    TickType_t start = xTaskGetTickCount();
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "capture: mutex timeout (5s)");
        return ERR_CAMERA_BUSY;
    }
    ESP_LOGI(TAG, "capture: mutex acquired (%lums)",
        (unsigned long)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));

    int ret = 0;
    uint8_t *data = NULL;
    size_t len = 0;

    ESP_LOGI(TAG, "capture: calling BSP camera_capture()...");
    start = xTaskGetTickCount();
    esp_err_t err = camera_capture(&data, &len);
    ESP_LOGI(TAG, "capture: BSP returned %d (%lums)", err,
        (unsigned long)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Capture failed: 0x%x", err);
        ret = ERR_CAMERA_CAPTURE_TIMEOUT;
        goto exit;
    }

    rgb565->data     = data;
    rgb565->len      = len;
    rgb565->capacity = len;
    rgb565->width    = camera_get_width();
    rgb565->height   = camera_get_height();

    ESP_LOGI(TAG, "Captured %dx%d RGB565 (%u bytes)", rgb565->width, rgb565->height, (unsigned)len);

exit:
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "capture: mutex released, ret=%d", ret);
    return ret;
}

/* ═══════════════════════════════════════════════
   JPEG 编码
   ═══════════════════════════════════════════════ */

int camera_manager_encode_jpeg(const vision_blob_t *rgb565, vision_blob_t *jpeg)
{
    if (!rgb565 || !rgb565->data || !jpeg) return ERR_CAMERA_JPEG_ENCODE;
    if (!camera_is_connected()) return ERR_CAMERA_NOT_READY;

    int ret = 0;
    uint8_t *jpeg_out = NULL;
    size_t jpeg_len = 0;

    ESP_LOGI(TAG, "encode: RGB565 %u bytes → JPEG...", (unsigned)rgb565->len);
    TickType_t start = xTaskGetTickCount();
    esp_err_t err = camera_rgb565_to_jpeg(rgb565->data, rgb565->len, &jpeg_out, &jpeg_len);
    ESP_LOGI(TAG, "encode: returned %d (%lums)", err,
        (unsigned long)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JPEG encode failed: 0x%x", err);
        ret = ERR_CAMERA_JPEG_ENCODE;
        goto exit;
    }

    jpeg->data     = jpeg_out;
    jpeg->len      = jpeg_len;
    jpeg->capacity = jpeg_len;
    jpeg->width    = rgb565->width;
    jpeg->height   = rgb565->height;

    ESP_LOGI(TAG, "JPEG encoded: %u bytes", (unsigned)jpeg_len);

exit:
    return ret;
}

/* ═══════════════════════════════════════════════
   Blob 释放
   ═══════════════════════════════════════════════ */

void camera_manager_blob_free(vision_blob_t *blob)
{
    if (!blob || !blob->data) return;
    free(blob->data);
    blob->data     = NULL;
    blob->len      = 0;
    blob->capacity = 0;
}

/* ═══════════════════════════════════════════════
   Preview Task (内部)
   ═══════════════════════════════════════════════ */

static void _preview_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "preview: task started");

    /* 启动连续 CSI 流 */
    esp_err_t err = camera_start_streaming();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "preview: camera_start_streaming failed: 0x%x", err);
        s_preview_running = false;
        vTaskDelete(NULL);
        return;
    }

    int frame_count = 0;
    TickType_t last_log = xTaskGetTickCount();

    while (s_preview_running) {
        uint8_t *data = NULL;
        size_t   len  = 0;

        err = camera_get_latest_frame(&data, &len);
        if (err != ESP_OK) {
            /* 超时 (100ms 内无新帧) — 正常, 继续等 */
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        /* 持锁更新预览帧 + 检查冻结状态 */
        xSemaphoreTake(s_freeze_mutex, portMAX_DELAY);

        /* resume 后重置帧率计数 (避免 fps 统计包含冻结期) */
        if (s_fps_reset) {
            s_fps_reset = false;
            frame_count = 0;
            last_log = xTaskGetTickCount();
        }

        if (s_frozen) {
            /* 冻结中: 丢弃帧, 降低轮询频率 */
            xSemaphoreGive(s_freeze_mutex);
            free(data);
            vTaskDelay(pdMS_TO_TICKS(100));  /* 100ms vs 33ms, 减少 CPU 浪费 */
            continue;
        }

        /* 非冻结: 替换预览 buffer (指针转移, 0 memcpy) */
        if (s_preview_frame.data) {
            free(s_preview_frame.data);
        }
        s_preview_frame.data     = data;
        s_preview_frame.len      = len;
        s_preview_frame.capacity = len;
        s_preview_frame.width    = camera_get_width();
        s_preview_frame.height   = camera_get_height();

        /* 投递回调 (在锁内调用, 保证 frame 指针有效) */
        if (s_preview_cb) {
            s_preview_cb(&s_preview_frame, s_preview_ctx);
        }

        xSemaphoreGive(s_freeze_mutex);

        frame_count++;

        /* 每 5 秒打印一次帧率 */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_log) >= pdMS_TO_TICKS(5000)) {
            float fps = (float)frame_count * 1000.0f /
                        (float)((now - last_log) * portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "preview: %d frames in %lums (%.1f fps)",
                     frame_count,
                     (unsigned long)((now - last_log) * portTICK_PERIOD_MS),
                     fps);
            frame_count = 0;
            last_log = now;
        }

        /* 帧间隔 (~30fps) */
        vTaskDelay(pdMS_TO_TICKS(PREVIEW_PERIOD_MS));
    }

    /* 停止 CSI 流 */
    camera_stop_streaming();
    ESP_LOGI(TAG, "preview: task exited");
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════
   Preview API
   ═══════════════════════════════════════════════ */

int camera_manager_preview_start(camera_preview_cb_t cb, void *user_data)
{
    if (!cb) return ERR_CAMERA_NOT_READY;
    if (!camera_is_connected()) {
        ESP_LOGE(TAG, "preview: camera not connected");
        return ERR_CAMERA_NOT_READY;
    }
    if (s_preview_running) {
        ESP_LOGW(TAG, "preview: already running");
        return 0;
    }

    s_preview_cb  = cb;
    s_preview_ctx = user_data;
    s_frozen      = false;
    s_preview_running = true;

    /* 清零旧状态 */
    if (s_preview_frame.data) {
        free(s_preview_frame.data);
        memset(&s_preview_frame, 0, sizeof(s_preview_frame));
    }
    if (s_frozen_frame.data) {
        free(s_frozen_frame.data);
        memset(&s_frozen_frame, 0, sizeof(s_frozen_frame));
    }

    BaseType_t ret = xTaskCreate(_preview_task, "cam_preview",
                                  PREVIEW_TASK_STACK, NULL,
                                  PREVIEW_TASK_PRIO, &s_preview_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "preview: task create failed");
        s_preview_running = false;
        s_preview_cb = NULL;
        return ERR_CAMERA_BUSY;
    }

    ESP_LOGI(TAG, "preview: started (task=%p, cb=%p)", (void*)s_preview_task, (void*)cb);
    return 0;
}

int camera_manager_preview_stop(void)
{
    if (!s_preview_running) {
        return 0;
    }

    ESP_LOGI(TAG, "preview: stopping...");
    s_preview_running = false;

    /* 等待 task 退出 (最多 2 秒) */
    if (s_preview_task) {
        TickType_t start = xTaskGetTickCount();
        while (eTaskGetState(s_preview_task) != eDeleted &&
               eTaskGetState(s_preview_task) != eReady) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(2000)) {
                ESP_LOGW(TAG, "preview: task delete timeout, forcing...");
                vTaskDelete(s_preview_task);
                break;
            }
        }
        s_preview_task = NULL;
    }

    /* 释放 buffer */
    xSemaphoreTake(s_freeze_mutex, portMAX_DELAY);
    if (s_preview_frame.data) {
        free(s_preview_frame.data);
        memset(&s_preview_frame, 0, sizeof(s_preview_frame));
    }
    if (s_frozen_frame.data) {
        free(s_frozen_frame.data);
        memset(&s_frozen_frame, 0, sizeof(s_frozen_frame));
    }
    s_frozen = false;
    s_preview_cb = NULL;
    s_preview_ctx = NULL;
    xSemaphoreGive(s_freeze_mutex);

    ESP_LOGI(TAG, "preview: stopped");
    return 0;
}

int camera_manager_preview_freeze(void)
{
    if (!s_preview_running) {
        ESP_LOGE(TAG, "preview: freeze failed — not running");
        return ERR_CAMERA_NOT_READY;
    }
    if (s_frozen) {
        ESP_LOGW(TAG, "preview: already frozen");
        return 0;
    }

    xSemaphoreTake(s_freeze_mutex, portMAX_DELAY);

    /* 所有权转移: preview_frame → frozen_frame (0 memcpy) */
    memcpy(&s_frozen_frame, &s_preview_frame, sizeof(s_frozen_frame));
    memset(&s_preview_frame, 0, sizeof(s_preview_frame));

    s_frozen = true;

    xSemaphoreGive(s_freeze_mutex);

    ESP_LOGI(TAG, "preview: frozen (frame %dx%d, %u bytes)",
             s_frozen_frame.width, s_frozen_frame.height,
             (unsigned)s_frozen_frame.len);
    return 0;
}

int camera_manager_preview_resume(void)
{
    if (!s_preview_running) {
        ESP_LOGE(TAG, "preview: resume failed — not running");
        return ERR_CAMERA_NOT_READY;
    }

    xSemaphoreTake(s_freeze_mutex, portMAX_DELAY);

    s_frozen = false;
    s_fps_reset = true;  /* 通知 preview task 重置帧率计数器 */

    if (s_frozen_frame.data) {
        free(s_frozen_frame.data);
        memset(&s_frozen_frame, 0, sizeof(s_frozen_frame));
    }

    xSemaphoreGive(s_freeze_mutex);

    ESP_LOGI(TAG, "preview: resumed");
    return 0;
}

const vision_blob_t *camera_manager_get_frozen_frame(void)
{
    if (!s_frozen || !s_frozen_frame.data) {
        return NULL;
    }
    return &s_frozen_frame;
}

bool camera_manager_is_preview_active(void)
{
    return s_preview_running;
}
