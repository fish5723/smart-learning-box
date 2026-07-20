/**
 * @file word_king_ui.h
 * @brief 单词王者 游戏 UI 接口
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 UI（懒加载，不在此创建屏幕）
 */
void word_king_ui_init(void);

/**
 * @brief 显示单词王者页面（首次调用时创建屏幕并开局）
 */
void word_king_ui_show(void);

/**
 * @brief 隐藏页面（屏幕对象保留）
 */
void word_king_ui_hide(void);

#ifdef __cplusplus
}
#endif
