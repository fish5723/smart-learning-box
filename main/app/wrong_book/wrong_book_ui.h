/**
 * @file wrong_book_ui.h
 * @brief 错题本 UI — LVGL 浏览/复习/删除页面
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化错题本 UI (延迟创建屏幕)
 */
void wrong_book_ui_init(void);

/**
 * @brief 显示错题本页面 (自动加载最新数据)
 */
void wrong_book_ui_show(void);

/**
 * @brief 隐藏错题本页面
 */
void wrong_book_ui_hide(void);

#ifdef __cplusplus
}
#endif
