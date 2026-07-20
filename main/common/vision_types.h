/**
 * @file vision_types.h
 * @brief Vision AI Platform — 统一数据结构
 *
 * vision_blob_t: 带容量的二进制数据块，支持内存复用
 * vision_result_t: 固定数组，不走 malloc
 * vision_endpoint_t: 扩展配置，新增 Vision 任务不用改其他代码
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════
   Blob — 带容量的二进制块 (支持复用检查)
   ═══════════════════════════════════════════════ */
typedef struct {
    uint8_t   *data;
    size_t     len;
    size_t     capacity;
    int        width;
    int        height;
} vision_blob_t;

/* ═══════════════════════════════════════════════
   Result — 固定数组 (零动态分配)
   ═══════════════════════════════════════════════ */
#define VISION_MAX_TAGS     8
#define VISION_TAG_MAX_LEN  32
#define VISION_TEXT_MAX_LEN 2048

typedef struct {
    char  question[VISION_TEXT_MAX_LEN];
    char  answer[VISION_TEXT_MAX_LEN];             /* 最终答案 (2048B, 与 question/explanation 一致) */
    char  explanation[VISION_TEXT_MAX_LEN];
    char  tags[VISION_MAX_TAGS][VISION_TAG_MAX_LEN];
    int   tag_count;
    char  raw_response[VISION_TEXT_MAX_LEN];
} vision_result_t;

/* ═══════════════════════════════════════════════
   OCR UI Result — 从 vision_result_t 提取, UI 层使用
   ═══════════════════════════════════════════════ */
typedef struct {
    char  question[VISION_TEXT_MAX_LEN];
    char  answer[VISION_TEXT_MAX_LEN];             /* 2048B — 与 vision_result_t 一致, 防止长答案截断 */
    char  explanation[VISION_TEXT_MAX_LEN];
    char  tags[VISION_MAX_TAGS][VISION_TAG_MAX_LEN];
    int   tag_count;
} ocr_result_t;

/* ═══════════════════════════════════════════════
   Endpoint — 每个 Vision 任务一个实例
   ═══════════════════════════════════════════════ */
typedef struct {
    const char *api_url;
    const char *api_key;
    const char *model;
} vision_endpoint_t;

#ifdef __cplusplus
}
#endif
