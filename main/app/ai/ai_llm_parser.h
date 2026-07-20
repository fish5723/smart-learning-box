/**
 * @file ai_llm_parser.h
 * @brief LLM SSE 响应解析 — 完全独立于 Vision 模块
 *
 * 只做一件事: 解析 SSE data: {...} 行, 提取 choices[0].delta.content
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SSE_DATA,    /* 返回一段 content */
    SSE_DONE,    /* [DONE] — 流结束 */
    SSE_ERR,     /* 解析错误, 跳过此行 */
} sse_result_t;

/**
 * @brief 解析一行 SSE data
 * @param line        输入行 ("data: {...}")
 * @param content_out 输出 buffer (caller 分配)
 * @param content_max buffer 大小
 * @return SSE_DATA (content_out 有内容) / SSE_DONE / SSE_ERR
 */
sse_result_t llm_parser_next(const char *line, char *content_out, size_t content_max);

#ifdef __cplusplus
}
#endif
