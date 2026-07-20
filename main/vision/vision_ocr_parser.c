/**
 * @file vision_ocr_parser.c
 * @brief OCR 响应解析 — 纯解析, 不拼文本/不排 Markdown/不排序标签
 *
 * 支持两种常见 OCR API 响应格式:
 *   A) choices[0].message.content  (OpenAI 兼容)
 *   B) { question, answer, explanation, tags[] }  (结构化)
 */
#include "vision_ocr_parser.h"
#include "common/error_code.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "VISION_OCR";

/* ── 安全拷贝 (带截断告警) ── */
static void _safe_copy(char *dst, const char *src, size_t max)
{
    if (!src || !dst) return;
    size_t src_len = strlen(src);
    if (src_len >= max) {
        ESP_LOGW("VISION_OCR", "String truncated: %u → %u bytes",
                 (unsigned)src_len, (unsigned)(max - 1));
    }
    strncpy(dst, src, max - 1);
    dst[max - 1] = '\0';
}

int ocr_parser_parse(const char *json_str, vision_result_t *out)
{
    if (!json_str || !out) return ERR_JSON_PARSE;

    memset(out, 0, sizeof(*out));
    _safe_copy(out->raw_response, json_str, VISION_TEXT_MAX_LEN);

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ERR_JSON_PARSE;
    }

    /* 格式 A: choices[0].message.content (OpenAI 兼容, 内层 JSON) */
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_GetArraySize(choices) > 0) {
        cJSON *c0   = cJSON_GetArrayItem(choices, 0);
        cJSON *msg  = cJSON_GetObjectItem(c0, "message");
        cJSON *cont = cJSON_GetObjectItem(msg, "content");
        if (cont && cJSON_IsString(cont)) {
            /* 尝试解析内层 JSON (结构化输出) */
            cJSON *inner = cJSON_Parse(cont->valuestring);
            if (inner) {
                /* 格式 A-1: content 内是标准 JSON → 递归提取 */
                cJSON *q = cJSON_GetObjectItem(inner, "question");
                cJSON *a = cJSON_GetObjectItem(inner, "answer");
                cJSON *e = cJSON_GetObjectItem(inner, "explanation");
                cJSON *tags = cJSON_GetObjectItem(inner, "tags");

                if (q && cJSON_IsString(q) && q->valuestring[0])
                    _safe_copy(out->question, q->valuestring, VISION_TEXT_MAX_LEN);
                if (a && cJSON_IsString(a))
                    _safe_copy(out->answer, a->valuestring, sizeof(out->answer));
                if (e && cJSON_IsString(e))
                    _safe_copy(out->explanation, e->valuestring, VISION_TEXT_MAX_LEN);
                if (tags && cJSON_IsArray(tags)) {
                    int n = cJSON_GetArraySize(tags);
                    if (n > VISION_MAX_TAGS) n = VISION_MAX_TAGS;
                    for (int i = 0; i < n; i++) {
                        cJSON *t = cJSON_GetArrayItem(tags, i);
                        if (t && cJSON_IsString(t))
                            _safe_copy(out->tags[i], t->valuestring, VISION_TAG_MAX_LEN);
                    }
                    out->tag_count = n;
                }
                cJSON_Delete(inner);
                ESP_LOGI(TAG, "Parsed (format A-structured)");
            } else {
                /* 格式 A-2: content 是纯文本 → 直接作为 question */
                _safe_copy(out->question, cont->valuestring, VISION_TEXT_MAX_LEN);
                ESP_LOGI(TAG, "Parsed (format A-plain)");
            }
        }
    }

    /* 格式 B: 顶层 question / answer / explanation / tags[] */
    cJSON *q = cJSON_GetObjectItem(root, "question");
    if (q && cJSON_IsString(q) && q->valuestring[0]) {
        _safe_copy(out->question, q->valuestring, VISION_TEXT_MAX_LEN);
    }
    cJSON *a = cJSON_GetObjectItem(root, "answer");
    if (a && cJSON_IsString(a)) {
        _safe_copy(out->answer, a->valuestring, sizeof(out->answer));
    }
    cJSON *e = cJSON_GetObjectItem(root, "explanation");
    if (e && cJSON_IsString(e)) {
        _safe_copy(out->explanation, e->valuestring, VISION_TEXT_MAX_LEN);
    }

    /* Tags */
    cJSON *tags = cJSON_GetObjectItem(root, "tags");
    if (tags && cJSON_IsArray(tags)) {
        int n = cJSON_GetArraySize(tags);
        if (n > VISION_MAX_TAGS) n = VISION_MAX_TAGS;
        for (int i = 0; i < n; i++) {
            cJSON *t = cJSON_GetArrayItem(tags, i);
            if (t && cJSON_IsString(t)) {
                _safe_copy(out->tags[i], t->valuestring, VISION_TAG_MAX_LEN);
            }
        }
        out->tag_count = n;
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Parse done: q_len=%u ans_len=%u exp_len=%u tags=%d",
             (unsigned)strlen(out->question),
             (unsigned)strlen(out->answer),
             (unsigned)strlen(out->explanation),
             out->tag_count);

    if (out->question[0] == '\0') {
        ESP_LOGW(TAG, "No question text found in response");
        return ERR_JSON_MISSING_FIELD;
    }

    return 0;
}

/* ═══════════════════════════════════════════════
   ocr_result_t — UI 层提取 (带保护)
   ═══════════════════════════════════════════════ */

void ocr_result_populate(const vision_result_t *src, ocr_result_t *dst)
{
    if (!src || !dst) return;
    memset(dst, 0, sizeof(*dst));

    /* Question: 为空则显示 fallback */
    if (src->question[0]) {
        strncpy(dst->question, src->question, VISION_TEXT_MAX_LEN - 1);
    } else {
        strncpy(dst->question, "未识别到题目", VISION_TEXT_MAX_LEN - 1);
    }

    /* Answer */
    if (src->answer[0]) {
        strncpy(dst->answer, src->answer, sizeof(dst->answer) - 1);
    }
    /* answer 为空时保持空字符串, UI 自行隐藏 */

    /* Explanation: 为空则显示 fallback */
    if (src->explanation[0]) {
        strncpy(dst->explanation, src->explanation, VISION_TEXT_MAX_LEN - 1);
    } else {
        strncpy(dst->explanation, "暂无解析", VISION_TEXT_MAX_LEN - 1);
    }

    /* Tags */
    dst->tag_count = src->tag_count;
    for (int i = 0; i < src->tag_count && i < VISION_MAX_TAGS; i++) {
        if (src->tags[i][0]) {
            strncpy(dst->tags[i], src->tags[i], VISION_TAG_MAX_LEN - 1);
        }
    }
    /* 重新计数 (跳过空 tag) */
    dst->tag_count = 0;
    for (int i = 0; i < VISION_MAX_TAGS; i++) {
        if (dst->tags[i][0]) dst->tag_count++;
        else break;
    }
}