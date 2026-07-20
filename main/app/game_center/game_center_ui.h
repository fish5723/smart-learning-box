/**
 * @file game_center_ui.h
 * @brief 游戏中心 UI 层 — LVGL 页面创建、事件处理
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化游戏中心 UI 页面
 */
void game_center_ui_init(void);

/**
 * @brief 显示游戏中心页面
 */
void game_center_ui_show(void);

/**c
 * @brief 隐藏游戏中心页面
 */
void game_center_ui_hide(void);

#ifdef __cplusplus
}
#endif
