/**
 * @file game_center.h
 * @brief 游戏中心模块 — 游戏入口与列表管理
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 游戏中心初始化
 *
 * 创建游戏中心 UI 页面，包含：
 *   - 顶部导航栏（返回 / 标题 / 状态）
 *   - 欢迎 Banner（游戏化学习中心 / 积分显示）
 *   - 游戏入口网格（数学2048 / 数学答题赛 / 单词王者 / 更多游戏）
 *   - 底部统计栏（累计挑战 / 游戏积分 / 最高连胜 / 游戏等级）
 */
void game_center_init(void);

/**
 * @brief 显示游戏中心页面
 */
void game_center_show(void);

/**
 * @brief 隐藏游戏中心页面
 */
void game_center_hide(void);

#ifdef __cplusplus
}
#endif
