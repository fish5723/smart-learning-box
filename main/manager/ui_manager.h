/**
 * @file ui_manager.h
 * @brief 最小化页面管理器 — 统一页面路由入口
 *
 * 职责: 提供单一页面切换入口，解耦 boot_ui / system_init 与各页面模块。
 * 不管理页面生命周期 (创建/销毁仍由各模块自行控制)。
 *
 * 使用:
 *   ui_manager_show_page(PAGE_HOME);   // 初始化并显示首页
 *   ui_manager_show_page(PAGE_AI);     // 显示 AI 页面
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAGE_HOME,
    PAGE_AI,
    PAGE_OCR,
    PAGE_ACHIEVEMENT,
    PAGE_GAME_CENTER,
    PAGE_WIFI,
    PAGE_WRONG_BOOK,
    PAGE_PHOTO_HISTORY,
    PAGE_OCR_ALBUM,
} ui_page_t;

/**
 * @brief 初始化页面管理器
 *
 * 在系统启动早期调用，注册所有页面模块。
 * 当前仅记录初始化完成标记，实际 init 由各模块自行处理。
 */
void ui_manager_init(void);

/**
 * @brief 显示指定页面
 *
 * PAGE_HOME 在首次调用时自动执行 home_init() + wifi_ui_init()。
 * 其他页面仅调用 show() (假设已由 init 阶段初始化)。
 *
 * @param page 目标页面
 */
void ui_manager_show_page(ui_page_t page);

#ifdef __cplusplus
}
#endif
