#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 系统初始化 —— BSP、LVGL、APP 初始化后创建双任务
 */
void system_init(void);

/**
 * @brief 获取累计学习时长（分钟）
 * @return 总分钟数（含后台待计入的累积）
 */
int system_get_study_minutes(void);

#ifdef __cplusplus
}
#endif
