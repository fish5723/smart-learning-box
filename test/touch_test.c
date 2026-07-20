/**
 * @file touch_test.c
 * @brief C6 固件版本测试 — 独立入口
 * 
 * 此文件作为 app_main 的替代入口，只执行 C6 版本查询
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

/* esp_hosted 头文件 */
#include "esp_hosted.h"
#include "esp_hosted_api_types.h"
#include "nvs_flash.h"

static const char *TAG = "C6_TEST";

/**
 * @brief 查询并打印 C6 固件版本
 */
static void c6_version_test(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "C6 Firmware Version Query Start");
    ESP_LOGI(TAG, "========================================");

    esp_hosted_coprocessor_fwver_t ver;
    memset(&ver, 0, sizeof(ver));

    esp_err_t ret = esp_hosted_get_coprocessor_fwversion(&ver);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "C6 Firmware Version: %lu.%lu.%lu", 
                 ver.major1, ver.minor1, ver.patch1);
        ESP_LOGI(TAG, "Version String: v%lu.%lu.%lu", 
                 ver.major1, ver.minor1, ver.patch1);
    } else {
        ESP_LOGE(TAG, "Failed to get C6 version: %s (0x%x)", 
                 esp_err_to_name(ret), ret);
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "C6 Firmware Version Query End");
    ESP_LOGI(TAG, "========================================");
}

/**
 * @brief 测试任务
 */
static void c6_test_task(void *pvParameters)
{
    (void)pvParameters;
    
    /* 等待系统稳定 */
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    c6_version_test();
    
    /* 任务结束 */
    vTaskDelete(NULL);
}

/**
 * @brief 独立入口 — 替代 app_main
 */
void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  C6 Firmware Version Test");
    ESP_LOGI(TAG, "============================================");

    /* ── 芯片信息 ── */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Chip: %d CPU cores, WiFi%s%s, silicon rev v%d.%d",
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "/bgn" : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             chip_info.revision / 100, chip_info.revision % 100);

    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash: %lu MB %s",
                 (unsigned long)(flash_size >> 20),
                 (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "(embedded)" : "(external)");
    }
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    /* 初始化 NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    /* 初始化 esp_hosted (SDIO + RPC) */
    ESP_LOGI(TAG, "Initializing esp_hosted...");
    ret = esp_hosted_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_init() failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "esp_hosted initialized");

    /* 等待 SDIO 连接稳定 — 需要等待 C6 从设备就绪 */
    ESP_LOGI(TAG, "Waiting for SDIO transport ready...");
    int retry = 0;
    while (retry < 30) {  /* 最多等待 30 秒 */
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        retry++;
        ESP_LOGI(TAG, "Waiting... %d/30", retry);
    }
    ESP_LOGI(TAG, "SDIO transport should be ready");

    /* 启动 C6 版本测试任务 */
    xTaskCreate(c6_test_task, "c6_test", 4096, NULL, 5, NULL);

    /* app_main 结束，由测试任务接管 */
    ESP_LOGI(TAG, "app_main() done — test task running");
}