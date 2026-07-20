/**
 * @file ocr.c
 * @brief OCR 拍照搜题 — 门面层, 调用 vision_ocr
 */
#include "ocr.h"
#include "ocr_ui.h"
#include "vision/vision_ocr.h"
#include "vision/vision_core.h"
#include "esp_log.h"

static const char *TAG = "OCR";

void ocr_init(void)
{
    ESP_LOGI(TAG, "Init");
    vision_ocr_init();
    ocr_ui_init();
}

void ocr_show(void)
{
    ESP_LOGI(TAG, "Show");
    ocr_ui_show();
}

void ocr_recover_pending(void)
{
    /* vision_core 不产生 pending 文件; 保留接口兼容性 */
}
