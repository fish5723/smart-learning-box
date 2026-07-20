/**
 * @file ai_llm.h
 * @brief AI 老师 LLM 客户端接口
 *
 * 通过 OpenAI 兼容的 Chat Completions API 与云端大模型通信，
 * 支持 SSE 流式输出，实时显示到 UI。
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 LLM 客户端（加载 API Key，验证配置）
 */
void ai_llm_init(void);

/**
 * @brief 发送用户消息到 LLM，启动 SSE 流式接收
 *
 * 内部创建 FreeRTOS 任务处理 HTTP 请求，不阻塞调用者。
 * 流式内容通过 ai_ui_append_ai_chunk_async() 自动更新到 UI。
 *
 * @param message 用户输入的文本
 * @param subject 学科名称（如"数学"），用于构造 system prompt
 * @return true 任务启动成功；false 失败（如网络未就绪、API Key 未配置）
 */
bool ai_llm_send_message(const char *message, const char *subject);

/**
 * @brief 检查 LLM 是否正在流式响应中
 */
bool ai_llm_is_streaming(void);

/**
 * @brief 测试网络连接（HTTP GET 百度）
 *
 * 用于诊断网络连通性问题，结果通过 UI 显示。
 */
void ai_llm_test_network(void);

#ifdef __cplusplus
}
#endif