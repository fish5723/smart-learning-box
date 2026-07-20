#pragma once

#include "achievement_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 成就系统初始化
 */
void achievement_init(void);

/**
 * @brief 显示成就中心
 */
void achievement_show(void);

/**
 * @brief 增加经验值（通用积分更新）
 *
 * @param score 本次获得的经验值
 * @return 当前总经验值
 */
int achievement_update(int score);

/**
 * @brief 完成特定任务，更新对应统计和经验
 *
 * @param task  任务类型
 * @param count 完成数量（题目数/问答次数/分钟数/天数）
 * @return 当前总经验值
 */
int achievement_complete_task(achievement_task_t task, int count);

/**
 * @brief 获取当前等级
 */
int achievement_get_level(void);

/**
 * @brief 获取当前经验值
 */
int achievement_get_exp(void);

/**
 * @brief 获取已解锁徽章数量
 */
int achievement_get_unlocked_badge_count(void);

#ifdef __cplusplus
}
#endif