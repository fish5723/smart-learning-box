/**
 * @file ui_manager.c
 * @brief 最小化页面管理器实现
 *
 * 所有页面切换必须经过此入口，禁止各模块直接相互调用 show/hide。
 * 当前为最小实现 — 后续可扩展转场动画、页面栈等。
 */

#include "ui_manager.h"
#include "app/home/home.h"
#include "app/ai/ai.h"
#include "app/ocr/ocr.h"
#include "app/achievement/achievement.h"
#include "app/game_center/game_center.h"
#include "app/wifi/wifi_ui.h"
#include "app/wrong_book/wrong_book_ui.h"
#include "app/photos/photo_history_ui.h"
#include "app/ocr/ocr_album_ui.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"

static const char *TAG = "UI_MGR";

void ui_manager_init(void)
{
    ESP_LOGI(TAG, "ui_manager_init() — page routing ready");
    /* 各页面模块的 init() 已在 system_init.c 中统一调用。
     * 此处仅为未来扩展预留 (如页面栈、转场动画配置)。 */
}

void ui_manager_show_page(ui_page_t page)
{
    ESP_LOGI(TAG, "Show page: %d", (int)page);

    esp_lv_adapter_lock(-1);

    switch (page) {
    case PAGE_HOME:
        /* 首次调用: home_init() 创建 UI + 显示, wifi_ui_init() 创建 WiFi 页面 */
        home_init();
        wifi_ui_init();
        break;
    case PAGE_AI:
        ai_show();
        break;
    case PAGE_OCR:
        ocr_show();
        break;
    case PAGE_ACHIEVEMENT:
        achievement_show();
        break;
    case PAGE_GAME_CENTER:
        game_center_show();
        break;
    case PAGE_WIFI:
        /* WiFi 页面由 wifi_ui_init 创建，此处仅显示 */
        break;
    case PAGE_WRONG_BOOK:
        wrong_book_ui_show();
        break;
    case PAGE_PHOTO_HISTORY:
        photo_history_ui_show();
        break;
    case PAGE_OCR_ALBUM:
        ocr_album_ui_show(NULL, NULL);
        break;
    default:
        ESP_LOGW(TAG, "Unknown page: %d", (int)page);
        break;
    }

    esp_lv_adapter_unlock();
}
