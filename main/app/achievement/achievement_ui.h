/**
 * @file achievement_ui.h
 * @brief Achievement center UI layer — LVGL 9.x
 *
 * Based on Screen_Achievement.html prototype.
 */

#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════
   任务类型定义
   ═══════════════════════════════════════════════ */
typedef enum {
    ACHV_TASK_QUESTION,      /* 完成题目 */
    ACHV_TASK_AI_CHAT,       /* AI 问答 */
    ACHV_TASK_STUDY_MINUTE,  /* 学习时长（分钟） */
    ACHV_TASK_STUDY_DAY,     /* 连续学习天数 */
} achievement_task_t;

/* ═══════════════════════════════════════════════
   UI 生命周期
   ═══════════════════════════════════════════════ */
void achievement_ui_init(void);
void achievement_ui_show(void);
void achievement_ui_hide(void);

/**
 * @brief 保存当前成就数据到 NVS
 */
void achievement_ui_save(void);

/* ═══════════════════════════════════════════════
   用户数据查询接口
   ═══════════════════════════════════════════════ */

int achievement_ui_get_level(void);
int achievement_ui_get_exp(void);
int achievement_ui_get_exp_max(void);
int achievement_ui_get_streak(void);
int achievement_ui_get_questions(void);
int achievement_ui_get_ai_chats(void);
int achievement_ui_get_study_hours(void);

/* ═══════════════════════════════════════════════
   用户数据修改接口（自动保存到 NVS）
   ═══════════════════════════════════════════════ */

/**
 * @brief 增加经验值，自动处理升级
 *
 * @param exp 获得的经验值（必须 > 0）
 * @return 是否发生升级
 */
bool achievement_ui_add_exp(int exp);

/**
 * @brief 完成任务，增加对应统计和经验
 *
 * @param task 任务类型
 * @param count 完成次数（分钟/次数/天数）
 * @return 当前总经验
 */
int achievement_ui_complete_task(achievement_task_t task, int count);

/**
 * @brief 检查徽章是否已解锁
 *
 * @param index 徽章索引 (0 ~ badge_count-1)
 * @return true 已解锁
 */
bool achievement_ui_is_badge_unlocked(int index);

/* ═══════════════════════════════════════════════
   连续学习天数 — 真实日期驱动
   ═══════════════════════════════════════════════ */

/**
 * @brief 基于真实日期执行每日签到
 *
 * 规则:
 *   - 时间未同步: 跳过，不修改 streak
 *   - 今天已签到: 返回 false，不重复计数
 *   - 首次/连续/中断: streak 自动更新，返回 true
 *
 * 注意: 本函数不发放经验值，经验由 achievement_ui_complete_task 发放。
 *
 * @return true 今天成功签到 (streak 发生变化)
 */
bool achievement_check_daily_streak(void);

/**
 * @brief 启动时修复连续学习天数
 *
 * 如果设备关机多日导致 streak 中断，重置为 0。
 * 不发放经验，不影响 last_study_date。
 * 时间未同步时跳过。
 */
void achievement_reconcile_streak(void);

/**
 * @brief 获取已解锁徽章数量
 */
int achievement_ui_get_unlocked_badge_count(void);

#ifdef __cplusplus
}
#endif