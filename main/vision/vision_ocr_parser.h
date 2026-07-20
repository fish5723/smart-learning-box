/**
 * @file vision_ocr_parser.h
 * @brief OCR 响应解析 — 纯函数, 只解析 JSON, 不拼文本/不排 Markdown/不排序标签
 */
#pragma once

#include "common/vision_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 解析 OCR API 返回的 JSON → vision_result_t
 * @param json_str  API 返回的 JSON 字符串
 * @param out       输出结果 (固定数组, 不 malloc)
 * @return 0 成功 / ERR_JSON_PARSE / ERR_JSON_MISSING_FIELD
 */
int ocr_parser_parse(const char *json_str, vision_result_t *out);

/**
 * @brief 从 vision_result_t 提取 ocr_result_t (UI 层使用)
 *
 * 包含 NULL/空字符串保护:
 *   - question 为空 → "未识别到题目"
 *   - answer 为空   → "" (UI 自行隐藏答案区域)
 *   - explanation 为空 → "暂无解析"
 *   - tags 为空     → tag_count=0 (UI 自行隐藏标签区域)
 *
 * @param src  解析后的 vision_result_t
 * @param dst  输出 ocr_result_t
 */
void ocr_result_populate(const vision_result_t *src, ocr_result_t *dst);

#ifdef __cplusplus
}
#endif
