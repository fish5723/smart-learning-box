/**
 * @file home_ui.h
 * @brief 首页 UI 层 — LVGL 页面创建、事件处理
 *
 * 中文字体由 font_loader 模块管理 (参见 app/font_loader/font_loader.h)
 * 图标由 icon_loader 模块管理 (参见 app/icon_loader/icon_loader.h)
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化首页 UI
 */
void home_ui_init(void);

/**
 * @brief 显示首页
 */
void home_ui_show(void);

/**
 * @brief 隐藏首页
 */
void home_ui_hide(void);

/**
 * @brief 更新首页WiFi状态显示
 *
 * 根据全局变量 g_wifi_connected 更新顶部状态栏WiFi文字。
 * 由WiFi事件回调调用。
 */
void home_ui_update_wifi_status(void);

/**
 * @brief SD 卡就绪后热更新首页字体/图标
 *
 * SD 卡初始化成功后调用，从 TF 卡加载完整字体和 PNG 图标替换 fallback。
 */
void home_ui_update_fonts(void);

#ifdef __cplusplus
}
#endif
