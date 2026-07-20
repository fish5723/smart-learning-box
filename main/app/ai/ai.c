/**
 * @file ai.c
 * @brief AI老师模块 — 集成 LLM 云端对话
 */

#include "ai.h"
#include "ai_ui.h"
#include "ai_llm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AI";

void ai_init(void)
{
    ESP_LOGI(TAG, "ai_init()");
    ai_ui_init();
    ai_llm_init();
}

void ai_show(void)
{
    ESP_LOGI(TAG, "ai_show()");
    
    /* 延迟显示 AI 界面，确保 SD 卡 I/O 完成
       避免 SD 卡读取干扰 ESP32-C6 SDIO WiFi 通信 */
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ai_ui_show();
}

void ai_hide(void)
{
    ESP_LOGI(TAG, "ai_hide()");
    ai_ui_hide();
}