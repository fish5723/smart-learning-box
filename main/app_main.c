/**
 * @brief Smart Learning Box — 系统入口
 *
 * 启动流程:
 *   1. 打印芯片/Flash/堆信息
 *   2. 测试模式: 仅初始化 SD 卡 → 读写测试 → 结束
 *   3. 正常模式: system_init() → BSP → LVGL adapter → App → 双任务
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#if CONFIG_SMARTBOX_SDCARD_TEST_MODE
#include "bsp/storage/sd_card.h"
#else
#include "system_init.h"
#endif

static const char *TAG = "BOOT";

void app_main(void)
{
    /* 提前配置 GT911 INT 引脚为上拉高电平，确保上电时地址选择为 0x14 */
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << 21),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&int_cfg);
    gpio_set_level(21, 1);

    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  Smart Learning Box — ESP32-P4 Bring-up");
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

#if CONFIG_SMARTBOX_SDCARD_TEST_MODE
    /* ═══════════════════════════════════════════════
       SD 卡独立测试模式
       仅初始化 NVS + SD 卡, 不启动 LCD/触摸/WiFi/LVGL
       ═══════════════════════════════════════════════ */
    ESP_LOGI(TAG, "*** SD CARD TEST MODE ***");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    sd_card_run_test();

    ESP_LOGI(TAG, "Test complete. Rebooting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
#else
    /* ── 正常启动: system_init() 完整初始化 ── */
    system_init();
    ESP_LOGI(TAG, "app_main() done — system tasks running");
#endif
}