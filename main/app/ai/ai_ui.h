/**
 * @file ai_ui.h
 * @brief AI老师 UI 层接口
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ai_ui_init(void);
void ai_ui_show(void);
void ai_ui_hide(void);

/**
 * @brief 添加AI回复消息到聊天区（完整消息）
 * @param text 消息内容
 */
void ai_ui_add_ai_message(const char *text);

/**
 * @brief 追加AI回复内容（SSE 流式，可多次调用）
 * @param chunk 本次收到的文本片段
 * @param is_end 是否为最后一块
 */
void ai_ui_append_ai_chunk(const char *chunk, bool is_end);

/**
 * @brief 添加用户消息到聊天区
 * @param text 消息内容
 */
void ai_ui_add_user_message(const char *text);

/**
 * @brief [线程安全] 追加AI回复内容（通过 lv_async_call 投递到 LVGL 任务）
 *
 * 可从任意 FreeRTOS 任务调用，内部自动调度到 LVGL 线程执行。
 */
void ai_ui_append_ai_chunk_async(const char *chunk, bool is_end);

/**
 * @brief [线程安全] 添加AI完整消息（通过 lv_async_call 投递到 LVGL 任务）
 */
void ai_ui_add_ai_message_async(const char *text);

/**
 * @brief [线程安全] AI 问答成功一次 → 发放成就经验
 *
 * 由 llm_task 在正常结束(HTTP 200)后调用，内部经 lv_async_call
 * 投递到 LVGL 线程执行 achievement_complete_task(ACHV_TASK_AI_CHAT, 1)。
 */
void ai_ui_reward_ai_chat(void);

#ifdef __cplusplus
}
#endif