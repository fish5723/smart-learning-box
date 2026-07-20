/**
 * @file ai.h
 * @brief AI老师模块 — UI-only 接口（后端已移除）
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AI老师模块初始化（仅初始化 UI）
 */
void ai_init(void);

/**
 * @brief 显示AI老师页面
 */
void ai_show(void);

/**
 * @brief 隐藏AI老师页面
 */
void ai_hide(void);

#ifdef __cplusplus
}
#endif
