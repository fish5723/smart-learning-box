/**
 * @file home.h
 * @brief 首页模块 — 功能入口与状态展示
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 首页初始化
 *
 * 创建首页 UI 页面，包含：
 *   - 顶部状态栏（WiFi / Logo / 电量 / 时间）
 *   - 欢迎卡片（头像 / 欢迎语 / 等级进度）
 *   - 功能入口网格（AI老师 / 拍照解题 / 趣味游戏 / 成长中心）
 *   - 底部统计栏（连续学习 / 完成题目 / AI问答 / 本周目标）
 */
void home_init(void);

/**
 * @brief 显示首页
 */
void home_show(void);

/**
 * @brief 隐藏首页
 */
void home_hide(void);

#ifdef __cplusplus
}
#endif