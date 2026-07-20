/**
 * @file home.c
 * @brief 首页模块 — 业务逻辑与页面管理
 *
 * 职责：
 *   - 管理首页页面生命周期
 *   - 处理功能入口点击事件
 *   - 调用各 APP 模块接口
 */

#include "home.h"
#include "home_ui.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "esp_log.h"

static const char *TAG = "HOME";

void home_init(void)
{
    ESP_LOGI(TAG, "home_init()");
    /* 字体由 system_init.c 中的 font_loader_init() 统一加载 */
    home_ui_init();
    home_show();
}

void home_show(void)
{
    ESP_LOGI(TAG, "home_show()");
    home_ui_show();
}

void home_hide(void)
{
    ESP_LOGI(TAG, "home_hide()");
    home_ui_hide();
}