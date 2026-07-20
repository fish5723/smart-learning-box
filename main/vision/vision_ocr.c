/**
 * @file vision_ocr.c
 * @brief OCR 拍照搜题 — 两步流水线
 *
 * Step 1 (fn_upload):  JPEG → Qwen3-VL 识图  → 提取文字 → ctx->recognized_text
 * Step 2 (fn_process): 文字 → 豆包解题       → 解析     → vision_result_t
 */
#include "vision_ocr.h"
#include "vision_ocr_parser.h"
#include "manager/camera_manager.h"
#include "manager/http_manager.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <mbedtls/base64.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VISION_OCR";

/* ═══════════════════════════════════════════════
   内部数据结构 (per-task context)
   ═══════════════════════════════════════════════ */
#define OCR_RESP_BUF_SIZE  8192

typedef struct {
    vision_blob_t  jpeg;        /* JPEG 编码输出 (从 frozen frame 编码, 或外部导入) */
    bool           imported;    /* true = jpeg 由外部导入, _ocr_capture 跳过相机 */
    uint8_t       *resp_buf;
    size_t         resp_buf_sz;  /* 分配大小 (动态扩容时更新) */
    size_t         resp_len;

    /* Vision 端点 (Qwen3-VL: 看图识文) */
    char  vis_url[256];
    char  vis_key[128];
    char  vis_model[64];

    /* Solver 端点 (豆包: 解题推理) */
    char  sol_url[256];
    char  sol_key[128];
    char  sol_model[64];

    /* 中间结果: Qwen3-VL 识别出的文字 */
    char  recognized_text[VISION_TEXT_MAX_LEN];
} ocr_ctx_t;

/* ═══════════════════════════════════════════════
   OCR 文本质量检测 — 过滤乱码/无效识别
   ═══════════════════════════════════════════════ */

/**
 * @brief 检测 OCR 文本质量, 低质量文本直接返回 false
 *
 * 规则:
 * 1. NULL 或长度 < 2 → 无效
 * 2. 有效字符比例 < 30% → 无效 (如 ",,,,,,10")
 * 3. 去除首尾明显垃圾字符
 *
 * 有效字符: CJK汉字、ASCII字母/数字、数学符号、常用标点
 */
static bool _ocr_validate_text(const char *text, char *cleaned, size_t cleaned_max)
{
    if (!text || !cleaned || cleaned_max == 0) return false;

    size_t len = strlen(text);
    if (len < 2) return false;

    /* 统计有效字符比例 */
    int valid = 0;
    int total = 0;
    const char *p = text;
    while (*p) {
        unsigned char c = (unsigned char)*p;
        total++;

        /* ASCII 字母/数字 */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) {
            valid++;
        }
        /* 数学符号 + 常用标点 */
        else if (c == '+' || c == '-' || c == '=' || c == '?' ||
                 c == '.' || c == ',' || c == '(' || c == ')' ||
                 c == '[' || c == ']' || c == '{' || c == '}' ||
                 c == '<' || c == '>' || c == ':' || c == ';' ||
                 c == ' ' || c == '!' || c == '/' || c == '*' ||
                 c == '^' || c == '%' || c == '#' || c == '|' ||
                 c == '~' || c == '_' || c == '√' || c == 'π' ||
                 c == '∠' || c == '°' || c == '△' || c == '×' ||
                 c == '÷') {
            valid++;
        }
        /* 全角标点 U+3000 区域 (、。！？：；""''「」) */
        else if (c >= 0x80) {
            /* UTF-8 多字节: 检查 CJK (U+4E00-U+9FFF, U+3000-U+303F, U+FF00-U+FFEF) */
            if ((c & 0xE0) == 0xE0 && p[1] && p[2]) {
                uint32_t cp = ((uint32_t)(c & 0x0F) << 12) |
                              ((uint32_t)((unsigned char)p[1] & 0x3F) << 6) |
                              ((uint32_t)((unsigned char)p[2] & 0x3F));
                if ((cp >= 0x4E00 && cp <= 0x9FFF) ||  /* CJK */
                    (cp >= 0x3000 && cp <= 0x303F) ||  /* CJK 标点 */
                    (cp >= 0xFF00 && cp <= 0xFFEF) ||  /* 全角 */
                    (cp >= 0x2000 && cp <= 0x206F) ||  /* 通用标点 */
                    (cp >= 0x2200 && cp <= 0x22FF) ||  /* 数学运算符 */
                    (cp >= 0x2500 && cp <= 0x257F) ||  /* 制表符 */
                    (cp >= 0x25A0 && cp <= 0x25FF) ||  /* 几何图形 */
                    (cp >= 0x2600 && cp <= 0x26FF) ||  /* 杂项符号 */
                    (cp >= 0x00A0 && cp <= 0x00BF)) {  /* Latin-1 补充 */
                    valid++;
                }
                p += 2;  /* 循环末尾的 p++ 会跳过第3个字节 */
            }
        }
        p++;
    }

    if (total == 0) return false;

    float ratio = (float)valid / (float)total;
    ESP_LOGI(TAG, "OCR quality: %d/%d valid chars (%.0f%%)",
             valid, total, ratio * 100.0f);

    if (ratio < 0.30f) {
        ESP_LOGW(TAG, "OCR text rejected: valid ratio too low (%.0f%% < 30%%)",
                 ratio * 100.0f);
        return false;
    }

    /* 去除首尾垃圾, 保留核心文本 */
    const char *start = text;
    const char *end   = text + len;

    /* skip leading garbage: commas, spaces, non-CJK */
    while (start < end && (*start == ',' || *start == ' ' ||
           *start == '\n' || *start == '\r' || *start == '\t')) {
        start++;
    }
    /* skip trailing garbage */
    while (end > start && (*(end-1) == ',' || *(end-1) == ' ' ||
           *(end-1) == '\n' || *(end-1) == '\r' || *(end-1) == '\t')) {
        end--;
    }

    size_t cleaned_len = (size_t)(end - start);
    if (cleaned_len >= cleaned_max) cleaned_len = cleaned_max - 1;
    memcpy(cleaned, start, cleaned_len);
    cleaned[cleaned_len] = '\0';

    ESP_LOGI(TAG, "OCR text cleaned: \"%.80s\"", cleaned);
    return true;
}

/* ═══════════════════════════════════════════════
   Hook: fn_capture — 获取冻结帧 + JPEG 编码
   ═══════════════════════════════════════════════ */
static int _ocr_capture(void *user_data, vision_result_t *io)
{
    (void)io;
    ocr_ctx_t *ctx = (ocr_ctx_t *)user_data;
    if (!ctx) return ERR_VISION_CAPTURE_FAILED;

    /* 导入模式: JPEG 已预加载, 直接跳过相机 */
    if (ctx->imported) {
        if (!ctx->jpeg.data || ctx->jpeg.len == 0) {
            ESP_LOGE(TAG, "Imported JPEG missing");
            return ERR_VISION_CAPTURE_FAILED;
        }
        ESP_LOGI(TAG, "Using imported JPEG: %u bytes (skip camera)",
                 (unsigned)ctx->jpeg.len);
        return 0;
    }

    memset(&ctx->jpeg, 0, sizeof(ctx->jpeg));

    /* 获取 Camera Manager 已冻结的帧 (不重新 Capture) */
    const vision_blob_t *frozen = camera_manager_get_frozen_frame();
    if (!frozen || !frozen->data) {
        ESP_LOGE(TAG, "No frozen frame available");
        return ERR_VISION_CAPTURE_FAILED;
    }

    ESP_LOGI(TAG, "Using frozen frame: %dx%d RGB565 %u bytes",
             frozen->width, frozen->height, (unsigned)frozen->len);

    /* JPEG 硬件编码 */
    int ret = camera_manager_encode_jpeg(frozen, &ctx->jpeg);
    if (ret != 0) {
        ESP_LOGE(TAG, "JPEG encode failed: 0x%x", ret);
        return ERR_VISION_CAPTURE_FAILED;
    }

    ESP_LOGI(TAG, "Capture OK: %dx%d JPEG %u bytes (from frozen frame)",
             ctx->jpeg.width, ctx->jpeg.height, (unsigned)ctx->jpeg.len);
    return 0;
}

/* ═══════════════════════════════════════════════
   Helpers: base64 + JSON body 构建
   ═══════════════════════════════════════════════ */

static char *_jpeg_to_data_uri(const uint8_t *jpeg_data, size_t jpeg_len)
{
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, jpeg_data, jpeg_len);

    const char *prefix = "data:image/jpeg;base64,";
    size_t prefix_len = strlen(prefix);
    size_t total = prefix_len + b64_len + 1;

    char *uri = malloc(total);
    if (!uri) return NULL;

    memcpy(uri, prefix, prefix_len);
    size_t written = 0;
    mbedtls_base64_encode((unsigned char *)(uri + prefix_len),
                          b64_len, &written, jpeg_data, jpeg_len);
    uri[prefix_len + written] = '\0';

    ESP_LOGI(TAG, "Base64: JPEG %u → data URI %u chars",
             (unsigned)jpeg_len, (unsigned)(prefix_len + written));
    return uri;
}

/* 构建 Vision API JSON body (image_url + text prompt) */
static char *_build_vision_body(const char *model, const char *data_uri,
                                 const char *prompt_text)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddBoolToObject(root, "stream", false);
    cJSON_AddNumberToObject(root, "max_tokens", 1024);

    cJSON *messages = cJSON_CreateArray();
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");

    cJSON *content = cJSON_CreateArray();

    /* image_url */
    cJSON *img_part = cJSON_CreateObject();
    cJSON_AddStringToObject(img_part, "type", "image_url");
    cJSON *img_url_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(img_url_obj, "url", data_uri);
    cJSON_AddItemToObject(img_part, "image_url", img_url_obj);
    cJSON_AddItemToArray(content, img_part);

    /* text */
    cJSON *text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "type", "text");
    cJSON_AddStringToObject(text_part, "text", prompt_text);
    cJSON_AddItemToArray(content, text_part);

    cJSON_AddItemToObject(user_msg, "content", content);
    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(root, "messages", messages);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* 构建纯文本 Chat JSON body (可选 system prompt + temperature)
 * disable_thinking: true=关闭推理链, 大幅缩短响应时间 (20-35s → <10s) */
static char *_build_chat_body(const char *model, const char *system_prompt,
                               const char *user_text, int max_tokens,
                               float temperature, bool disable_thinking)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddBoolToObject(root, "stream", false);
    cJSON_AddNumberToObject(root, "max_tokens", max_tokens > 0 ? max_tokens : 256);
    if (temperature > 0.0f) {
        cJSON_AddNumberToObject(root, "temperature", (double)temperature);
    }

    /* 关闭推理模型的思考链 (reasoning_content), 大幅降低延迟 */
    if (disable_thinking) {
        cJSON *thinking = cJSON_CreateObject();
        if (thinking) {
            cJSON_AddStringToObject(thinking, "type", "disabled");
            cJSON_AddItemToObject(root, "thinking", thinking);
        }
    }

    cJSON *messages = cJSON_CreateArray();

    /* system message (optional) */
    if (system_prompt && system_prompt[0]) {
        cJSON *sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", system_prompt);
        cJSON_AddItemToArray(messages, sys_msg);
    }

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_text);
    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(root, "messages", messages);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

/* 从 Chat Completions 响应中提取 choices[0].message.content */
static int _extract_content(const char *json_str, char *out, size_t out_max)
{
    if (!json_str || !out || out_max == 0) return -1;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) return -1;

    int ret = -1;
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices) {
        cJSON *c0 = cJSON_GetArrayItem(choices, 0);
        if (c0) {
            cJSON *msg = cJSON_GetObjectItem(c0, "message");
            if (msg) {
                cJSON *cont = cJSON_GetObjectItem(msg, "content");
                if (cont && cJSON_IsString(cont) && cont->valuestring) {
                    strncpy(out, cont->valuestring, out_max - 1);
                    out[out_max - 1] = '\0';
                    ret = 0;
                }
            }
        }
    }

    cJSON_Delete(root);
    return ret;
}

/* 通用 HTTP POST JSON → 响应体 (内部动态缓冲, 不截断) */
static int _http_post_json(const char *url, const char *api_key,
                            const char *json_body, int body_len,
                            uint8_t **resp_buf_ptr, size_t *resp_buf_sz_ptr,
                            size_t *resp_len, int timeout_ms,
                            const char *log_label)
{
    http_handle_t *http = http_manager_create();
    if (!http) return ERR_VISION_UPLOAD_FAILED;

    ESP_LOGI(TAG, "[%s] POST → %s (%d bytes, timeout=%dms)",
             log_label, url, body_len, timeout_ms);

    /* 先用 caller 缓冲 (兼容), 不足时从内部缓冲读取 */
    http_req_t req = {
        .url          = url,
        .method       = "POST",
        .body         = json_body,
        .body_len     = body_len,
        .timeout_ms   = timeout_ms,
        .api_key      = api_key,
        .content_type = "application/json",
        .resp_buf     = *resp_buf_ptr,
        .resp_buf_sz  = *resp_buf_sz_ptr,
        .resp_len     = resp_len,
    };

    int status = 0;
    int ret = http_manager_post(http, &req, &status);

    if (ret != 0) {
        ESP_LOGE(TAG, "[%s] HTTP failed: 0x%x", log_label, ret);
        http_manager_destroy(http);
        return ERR_VISION_UPLOAD_FAILED;
    }

    if (status != 200) {
        /* 错误响应: 从内部缓冲读取完整内容 */
        const char *err_body = http_manager_get_body(http);
        size_t err_len = http_manager_get_body_len(http);
        ESP_LOGE(TAG, "[%s] HTTP %d: %.*s", log_label, status,
                 (int)(err_len > 256 ? 256 : err_len),
                 err_body ? err_body : "(null)");
        http_manager_destroy(http);
        return ERR_VISION_UPLOAD_FAILED;
    }

    /* 成功: 检查 caller 缓冲是否足够, 不足则重新分配 */
    size_t full_len = http_manager_get_body_len(http);
    const char *full_body = http_manager_get_body(http);

    if (full_body && full_len >= *resp_buf_sz_ptr) {
        /* Caller 缓冲不足 — 重新分配 */
        size_t new_size = full_len + 1;
        uint8_t *new_buf = realloc(*resp_buf_ptr, new_size);
        if (new_buf) {
            memcpy(new_buf, full_body, full_len);
            new_buf[full_len] = '\0';
            *resp_buf_ptr = new_buf;
            *resp_buf_sz_ptr = new_size;
            *resp_len = full_len;
        }
        /* realloc 失败: 保留截断数据, 已在 req.resp_buf 中 */
    }

    ESP_LOGI(TAG, "[%s] OK: %u bytes response", log_label, (unsigned)*resp_len);
    http_manager_destroy(http);
    return 0;
}

/* ═══════════════════════════════════════════════
   Hook: fn_upload — JPEG → Qwen3-VL 识图 → 提取文字
   ═══════════════════════════════════════════════ */
static int _ocr_upload(void *user_data, vision_result_t *io)
{
    (void)io;
    ocr_ctx_t *ctx = (ocr_ctx_t *)user_data;
    if (!ctx || !ctx->jpeg.data) return ERR_VISION_UPLOAD_FAILED;

    /* Step 1: JPEG → base64 data URI */
    char *data_uri = _jpeg_to_data_uri(ctx->jpeg.data, ctx->jpeg.len);
    /* JPEG 生命周期延长至 _ocr_cleanup: 供 _on_ocr_result 中 photo_history_save 使用 */
    if (!data_uri) {
        ESP_LOGE(TAG, "Base64 encode failed");
        return ERR_VISION_UPLOAD_FAILED;
    }

    /* Step 2: 构建 Qwen3-VL 识图请求 */
    const char *vis_prompt = "请识别图片中的文字内容。只输出识别到的文字，不要添加任何解释或额外内容。";
    char *json_body = _build_vision_body(ctx->vis_model, data_uri, vis_prompt);
    free(data_uri);
    if (!json_body) {
        ESP_LOGE(TAG, "Failed to build vision request body");
        return ERR_VISION_UPLOAD_FAILED;
    }
    int body_len = strlen(json_body);

    /* Step 3: HTTP POST → Qwen3-VL */
    ctx->resp_buf = malloc(OCR_RESP_BUF_SIZE);
    if (!ctx->resp_buf) {
        free(json_body);
        return ERR_VISION_UPLOAD_FAILED;
    }
    ctx->resp_buf_sz = OCR_RESP_BUF_SIZE;

    int ret = _http_post_json(ctx->vis_url, ctx->vis_key, json_body, body_len,
                               &ctx->resp_buf, &ctx->resp_buf_sz, &ctx->resp_len,
                               30000, "Qwen-VL");
    free(json_body);

    if (ret != 0) {
        free(ctx->resp_buf);
        ctx->resp_buf = NULL;
        return ERR_VISION_UPLOAD_FAILED;
    }

    /* Step 4: 提取识别文字 → ctx->recognized_text */
    ESP_LOGI(TAG, "Qwen-VL response(%uB): %.200s",
             (unsigned)ctx->resp_len, (const char *)ctx->resp_buf);
    if (_extract_content((const char *)ctx->resp_buf,
                          ctx->recognized_text,
                          VISION_TEXT_MAX_LEN) != 0) {
        ESP_LOGE(TAG, "Failed to extract text from Qwen-VL response");
        free(ctx->resp_buf);
        ctx->resp_buf = NULL;
        return ERR_VISION_UPLOAD_FAILED;
    }

    ESP_LOGI(TAG, "Recognized text(%u chars): %.100s",
             (unsigned)strlen(ctx->recognized_text), ctx->recognized_text);

    /* 释放 Qwen 响应缓冲, 准备给豆包用 */
    free(ctx->resp_buf);
    ctx->resp_buf = NULL;

    if (ctx->recognized_text[0] == '\0') {
        ESP_LOGE(TAG, "Qwen-VL returned empty text");
        return ERR_VISION_UPLOAD_FAILED;
    }

    return 0;
}

/* ═══════════════════════════════════════════════
   Hook: fn_process — 文字 → 豆包解题 → 解析
   ═══════════════════════════════════════════════ */

/* Solver 超时+重试参数 */
#define SOLVER_TIMEOUT_MS   60000
#define SOLVER_RETRY_DELAY_MS 1000

/* Solver System Prompt — 儿童学习助手 */
#define SOLVER_SYSTEM_PROMPT \
    "你是智能学习机AI解题助手。" \
    "任务：根据OCR识别文本帮助学生解决问题。" \
    "规则：" \
    "1. OCR文本可能包含错误、乱码、缺失。" \
    "2. 如果无法确定题目，不要编造答案。" \
    "3. 不输出思考过程。" \
    "4. explanation限制50字以内。" \
    "5. 适合儿童阅读。" \
    "6. 必须严格输出JSON，不允许Markdown。" \
    "成功格式：{\"question\":\"原题\",\"answer\":\"最终答案\",\"explanation\":\"解题步骤\"}" \
    "失败格式：{\"question\":\"\",\"answer\":\"\",\"explanation\":\"无法识别题目，请重新拍摄\"}"

/* 降级结果: OCR 质量不足时直接返回 */
static void _ocr_set_fallback_result(vision_result_t *io, const char *explanation)
{
    if (!io) return;
    io->question[0]   = '\0';
    io->answer[0]     = '\0';
    io->tag_count     = 0;
    strncpy(io->explanation, explanation ? explanation : "识别内容不清晰，请重新拍摄",
            sizeof(io->explanation) - 1);
    io->explanation[sizeof(io->explanation) - 1] = '\0';
}

static int _ocr_process(void *user_data, vision_result_t *io)
{
    ocr_ctx_t *ctx = (ocr_ctx_t *)user_data;
    if (!ctx || !io) return ERR_VISION_PROCESS_FAILED;
    if (ctx->recognized_text[0] == '\0') {
        _ocr_set_fallback_result(io, "识别内容为空，请重新拍摄");
        return 0;
    }

    /* Step 0: OCR 文本质量检测 — 低质量文本不调用豆包
     * 使用堆分配避免栈溢出 (vision_task 栈仅 8KB,
     *   原栈数组 cleaned[2048] + user_prompt[2560] ≈ 4.6KB 太危险) */
    char *cleaned = malloc(VISION_TEXT_MAX_LEN);
    if (!cleaned) {
        _ocr_set_fallback_result(io, "内存不足，请重试");
        return 0;
    }

    bool text_ok = _ocr_validate_text(ctx->recognized_text, cleaned, VISION_TEXT_MAX_LEN);
    if (!text_ok) {
        ESP_LOGW(TAG, "OCR text quality too low, skipping solver");
        _ocr_set_fallback_result(io, "识别内容不清晰，请重新拍摄");
        free(cleaned);
        return 0;
    }

    /* Step 1: 构建豆包解题请求 — System + User messages, temperature=0.2,
     * max_tokens=256, thinking=disabled (关闭推理链, 提速 3-5x) */
    size_t prompt_size = VISION_TEXT_MAX_LEN + 512;
    char *user_prompt = malloc(prompt_size);
    if (!user_prompt) {
        _ocr_set_fallback_result(io, "内存不足，请重试");
        free(cleaned);
        return 0;
    }

    snprintf(user_prompt, prompt_size,
        "请解答以下题目，严格输出JSON格式(不要markdown代码块):\n"
        "{\"question\":\"原题\",\"answer\":\"最终答案\",\"explanation\":\"解题步骤\"}\n\n"
        "题目：%s", cleaned);
    free(cleaned);

    char *json_body = _build_chat_body(ctx->sol_model, SOLVER_SYSTEM_PROMPT,
                                        user_prompt, 256, 0.2f, true);
    free(user_prompt);
    if (!json_body) {
        ESP_LOGE(TAG, "Failed to build solver request body");
        return ERR_VISION_PROCESS_FAILED;
    }
    int body_len = strlen(json_body);

    ESP_LOGI(TAG, "Solver request: %d bytes, thinking=disabled", body_len);

    /* Step 2: HTTP POST → 豆包 (60s timeout + 1 retry) */
    int ret = ERR_VISION_PROCESS_FAILED;
    for (int attempt = 0; attempt < 2; attempt++) {
        ctx->resp_buf = malloc(OCR_RESP_BUF_SIZE);
        if (!ctx->resp_buf) {
            free(json_body);
            return ERR_VISION_PROCESS_FAILED;
        }
        ctx->resp_buf_sz = OCR_RESP_BUF_SIZE;
        ctx->resp_len = 0;

        ESP_LOGI(TAG, "Solver attempt %d/%d (timeout=%dms)",
                 attempt + 1, 2, SOLVER_TIMEOUT_MS);

        ret = _http_post_json(ctx->sol_url, ctx->sol_key, json_body, body_len,
                               &ctx->resp_buf, &ctx->resp_buf_sz, &ctx->resp_len,
                               SOLVER_TIMEOUT_MS, "Doubao");

        if (ret == 0) break;  /* 成功, 跳出重试循环 */

        /* 失败: 释放缓冲, 重试前等待 */
        ESP_LOGW(TAG, "Solver attempt %d failed: 0x%x", attempt + 1, ret);
        free(ctx->resp_buf);
        ctx->resp_buf = NULL;

        if (attempt == 0) {
            ESP_LOGI(TAG, "Retrying in %dms...", SOLVER_RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(SOLVER_RETRY_DELAY_MS));
        }
    }
    free(json_body);

    if (ret != 0) {
        free(ctx->resp_buf);
        ctx->resp_buf = NULL;
        _ocr_set_fallback_result(io, "网络超时，请检查网络后重试");
        return 0;  /* 非致命: 让 UI 显示友好提示 */
    }

    /* Step 3: 解析豆包响应 → vision_result_t */
    ESP_LOGI(TAG, "Doubao response(%uB): %.200s",
             (unsigned)ctx->resp_len, (const char *)ctx->resp_buf);
    ret = ocr_parser_parse((const char *)ctx->resp_buf, io);

    free(ctx->resp_buf);
    ctx->resp_buf = NULL;

    if (ret != 0) {
        ESP_LOGE(TAG, "Parse failed: 0x%x", ret);
        _ocr_set_fallback_result(io, "AI解析异常，请重新拍摄");
        return 0;  /* 非致命 */
    }

    /* ★ 调试日志: 打印各字段长度, 帮助定位截断/UI 异常 */
    ESP_LOGI(TAG, "Process OK: q=%.60s (len=%u) ans=%.30s (len=%u) "
             "exp=%.40s (len=%u) tags=%d",
             io->question, (unsigned)strlen(io->question),
             io->answer, (unsigned)strlen(io->answer),
             io->explanation, (unsigned)strlen(io->explanation),
             io->tag_count);
    return 0;
}

/* ═══════════════════════════════════════════════
   Hook: fn_cleanup
   ═══════════════════════════════════════════════ */
static int _ocr_cleanup(void *user_data, vision_result_t *io)
{
    (void)io;
    ocr_ctx_t *ctx = (ocr_ctx_t *)user_data;
    if (!ctx) return 0;

    if (ctx->jpeg.data)   camera_manager_blob_free(&ctx->jpeg);
    if (ctx->resp_buf)    free(ctx->resp_buf);
    free(ctx);
    ESP_LOGI(TAG, "Cleanup complete");
    return 0;
}

/* ═══════════════════════════════════════════════
   Accessor: 供 _on_ocr_result 获取 JPEG 数据
   ═══════════════════════════════════════════════ */

const uint8_t *vision_ocr_get_jpeg_data(const void *user_data)
{
    const ocr_ctx_t *ctx = (const ocr_ctx_t *)user_data;
    return (ctx && ctx->jpeg.data) ? ctx->jpeg.data : NULL;
}

size_t vision_ocr_get_jpeg_size(const void *user_data)
{
    const ocr_ctx_t *ctx = (const ocr_ctx_t *)user_data;
    return (ctx && ctx->jpeg.data) ? ctx->jpeg.len : 0;
}

bool vision_ocr_is_imported(const void *user_data)
{
    const ocr_ctx_t *ctx = (const ocr_ctx_t *)user_data;
    return ctx ? ctx->imported : false;
}

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

int vision_ocr_init(void)
{
    ESP_LOGI(TAG, "Init (Qwen3-VL → Doubao pipeline)");
    return 0;
}

void vision_ocr_deinit(void)
{
    ESP_LOGI(TAG, "Deinit");
}

/* 分配 ctx 并复制双端点配置 (两个 start 入口共用) */
static ocr_ctx_t *_ocr_ctx_new(const vision_endpoint_t *vision_endpoint,
                               const vision_endpoint_t *solver_endpoint)
{
    ocr_ctx_t *ctx = calloc(1, sizeof(ocr_ctx_t));
    if (!ctx) return NULL;

    /* 复制 Vision 端点 (Qwen3-VL) */
    if (vision_endpoint->api_url) strncpy(ctx->vis_url, vision_endpoint->api_url, sizeof(ctx->vis_url) - 1);
    if (vision_endpoint->api_key) strncpy(ctx->vis_key, vision_endpoint->api_key, sizeof(ctx->vis_key) - 1);
    if (vision_endpoint->model)   strncpy(ctx->vis_model, vision_endpoint->model, sizeof(ctx->vis_model) - 1);

    /* 复制 Solver 端点 (豆包) */
    if (solver_endpoint->api_url) strncpy(ctx->sol_url, solver_endpoint->api_url, sizeof(ctx->sol_url) - 1);
    if (solver_endpoint->api_key) strncpy(ctx->sol_key, solver_endpoint->api_key, sizeof(ctx->sol_key) - 1);
    if (solver_endpoint->model)   strncpy(ctx->sol_model, solver_endpoint->model, sizeof(ctx->sol_model) - 1);

    return ctx;
}

/* 填充任务配置 (两个 start 入口共用) */
static void _ocr_fill_cfg(vision_task_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->name               = "OCR";
    cfg->fn_capture         = _ocr_capture;
    cfg->fn_upload          = _ocr_upload;
    cfg->fn_process         = _ocr_process;
    cfg->fn_cleanup         = _ocr_cleanup;
    cfg->capture_timeout_ms = 30000;
    cfg->upload_timeout_ms  = 60000;
    cfg->process_timeout_ms = 60000;
}

int vision_ocr_start(const vision_endpoint_t *vision_endpoint,
                     const vision_endpoint_t *solver_endpoint,
                     const vision_callback_t *cb, void *user_data)
{
    (void)user_data;
    if (!vision_endpoint || !vision_endpoint->api_url) return ERR_VISION_BUSY;
    if (!solver_endpoint || !solver_endpoint->api_url) return ERR_VISION_BUSY;

    ocr_ctx_t *ctx = _ocr_ctx_new(vision_endpoint, solver_endpoint);
    if (!ctx) return ERR_VISION_BUSY;

    ESP_LOGI(TAG, "Pipeline: %s (vision) → %s (solver)",
             ctx->vis_model, ctx->sol_model);

    vision_task_config_t cfg;
    _ocr_fill_cfg(&cfg);

    int ret = vision_start(&cfg, cb, ctx);
    if (ret != 0) {
        free(ctx);
    }

    return ret;
}

int vision_ocr_start_from_jpeg(const uint8_t *jpeg_data, size_t jpeg_len,
                               const vision_endpoint_t *vision_endpoint,
                               const vision_endpoint_t *solver_endpoint,
                               const vision_callback_t *cb, void *user_data)
{
    (void)user_data;
    if (!jpeg_data || jpeg_len == 0) return ERR_VISION_CAPTURE_FAILED;
    if (!vision_endpoint || !vision_endpoint->api_url) return ERR_VISION_BUSY;
    if (!solver_endpoint || !solver_endpoint->api_url) return ERR_VISION_BUSY;

    ocr_ctx_t *ctx = _ocr_ctx_new(vision_endpoint, solver_endpoint);
    if (!ctx) return ERR_VISION_BUSY;

    /* 复制导入的 JPEG (用 malloc, 与 camera_manager_blob_free 释放路径一致) */
    ctx->jpeg.data = malloc(jpeg_len);
    if (!ctx->jpeg.data) {
        ESP_LOGE(TAG, "Failed to alloc %u bytes for imported JPEG", (unsigned)jpeg_len);
        free(ctx);
        return ERR_VISION_CAPTURE_FAILED;
    }
    memcpy(ctx->jpeg.data, jpeg_data, jpeg_len);
    ctx->jpeg.len      = jpeg_len;
    ctx->jpeg.capacity = jpeg_len;
    ctx->imported      = true;

    ESP_LOGI(TAG, "Import pipeline: %s (vision) → %s (solver), JPEG=%uB",
             ctx->vis_model, ctx->sol_model, (unsigned)jpeg_len);

    vision_task_config_t cfg;
    _ocr_fill_cfg(&cfg);

    int ret = vision_start(&cfg, cb, ctx);
    if (ret != 0) {
        camera_manager_blob_free(&ctx->jpeg);
        free(ctx);
    }

    return ret;
}
