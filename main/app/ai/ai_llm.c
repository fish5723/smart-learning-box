/**
 * @file ai_llm.c
 * @brief AI 老师 LLM 客户端实现
 *
 * 使用 ESP-IDF esp_http_client 发送 OpenAI 兼容的 Chat Completions 请求，
 * 支持 SSE (Server-Sent Events) 流式输出。
 *
 * 官方参考:
 * - ESP HTTP Client: https://docs.espressif.com/projects/esp-idf/zh_CN/stable/esp32/api-reference/protocols/esp_http_client.html
 * - OpenAI Chat Completions: https://platform.openai.com/docs/api-reference/chat/create
 * - SSE 格式: data: {...}\n\n
 */

#include "ai_llm.h"
#include "ai_ui.h"
#include "bsp/storage/storage.h"
#include "bsp/wifi/wifi.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "AI_LLM";

/* ── 配置 ── */
#define LLM_TASK_STACK_SIZE     16384
#define LLM_TASK_PRIORITY       5
#define LLM_BUF_SIZE            2048
#define LLM_MAX_CONTENT_LEN     4096
#define LLM_TIMEOUT_MS          30000

/* 默认使用豆包火山引擎 (OpenAI 兼容)
   model 用账号的推理接入点 ID(ep-...), 已 curl 实测可用;
   直接用模型名(如 doubao-seed-2.1-pro)需账号已开通该模型, 否则 404 */
#define LLM_DEFAULT_BASE_URL    "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
#define LLM_DEFAULT_MODEL       "ep-20260710152809-mb4tr"

/* ── 运行时状态 ── */
static char s_api_key[128] = {0};
static bool s_initialized = false;
static volatile bool s_streaming = false;
static TaskHandle_t s_llm_task = NULL;

/* SSE 流式缓冲区（文件级作用域，与 s_streaming 标志共同保证互斥） */
static char s_sse_buf[LLM_BUF_SIZE];
static size_t s_sse_len = 0;

/* ── 请求体构造 ── */
static char *build_request_body(const char *message, const char *subject)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "model", LLM_DEFAULT_MODEL);
    cJSON_AddBoolToObject(root, "stream", true);

    /* 限制响应长度 + 关闭推理模型思考链。
     * ep-... 是推理模型(响应含 reasoning_content), 长流式会把 SDIO 下行持续时间
     * 拉长 → 放大 258 竞态窗口。限长 + 关思考可大幅缩短流, 降低 258 触发概率。 */
    cJSON_AddNumberToObject(root, "max_tokens", 256);
    cJSON *thinking = cJSON_CreateObject();
    if (thinking) {
        cJSON_AddStringToObject(thinking, "type", "disabled");
        cJSON_AddItemToObject(root, "thinking", thinking);
    }

    cJSON *messages = cJSON_CreateArray();
    if (messages) {
        /* system prompt: 设定角色和学科 */
        cJSON *sys_msg = cJSON_CreateObject();
        char sys_prompt[320];
        snprintf(sys_prompt, sizeof(sys_prompt),
                 "你是一位专业的%s老师，面向中小学生教学。"
                 "回答要简洁易懂，必要时使用例子说明。"
                 "每次回答控制在200字以内。"
                 "请使用纯文本回答，不要使用任何Markdown语法（如#,*,**,`,```等）。", subject ? subject : "全科");
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", sys_prompt);
        cJSON_AddItemToArray(messages, sys_msg);

        /* user message */
        cJSON *user_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(user_msg, "role", "user");
        cJSON_AddStringToObject(user_msg, "content", message);
        cJSON_AddItemToArray(messages, user_msg);

        cJSON_AddItemToObject(root, "messages", messages);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        ESP_LOGE(TAG, "Failed to serialize request body (out of memory)");
    }
    return body;
}

/* ── SSE 解析: 使用 ai_llm_parser ── */
#include "ai_llm_parser.h"

static void parse_sse_chunk(const char *line, size_t len)
{
    if (len < 6 || strncmp(line, "data: ", 6) != 0) {
        return;
    }

    char content[512];
    sse_result_t r = llm_parser_next(line, content, sizeof(content));

    if (r == SSE_DONE) {
        ai_ui_append_ai_chunk_async("", true);
    } else if (r == SSE_DATA) {
        ai_ui_append_ai_chunk_async(content, false);
    }
    /* SSE_ERR: silently skip */
}

/* ── HTTP 事件处理 ── */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA: {
        /* 将收到的数据追加到缓冲区，按行解析 SSE */
        size_t copy_len = evt->data_len;
        if (s_sse_len + copy_len >= LLM_BUF_SIZE - 1) {
            copy_len = LLM_BUF_SIZE - 1 - s_sse_len;
        }
        if (copy_len > 0) {
            memcpy(s_sse_buf + s_sse_len, evt->data, copy_len);
            s_sse_len += copy_len;
            s_sse_buf[s_sse_len] = '\0';

            /* 按 \n 分割解析 */
            char *line_start = s_sse_buf;
            char *p = s_sse_buf;
            while (*p) {
                if (*p == '\n') {
                    *p = '\0';
                    if (p > line_start && *(p-1) == '\r') {
                        *(p-1) = '\0';
                    }
                    parse_sse_chunk(line_start, strlen(line_start));
                    line_start = p + 1;
                }
                p++;
            }

            /* 保留未完整的一行 */
            if (line_start < s_sse_buf + s_sse_len) {
                size_t remain = s_sse_buf + s_sse_len - line_start;
                memmove(s_sse_buf, line_start, remain);
                s_sse_len = remain;
            } else {
                s_sse_len = 0;
            }
        }
        break;
    }

    case HTTP_EVENT_ON_FINISH:
        s_sse_len = 0;
        break;

    case HTTP_EVENT_DISCONNECTED:
        s_sse_len = 0;
        break;

    default:
        break;
    }
    return ESP_OK;
}

/* ── LLM 请求任务 ── */
static void llm_task(void *arg)
{
    char *message = (char *)arg;
    char subject[32] = "全科";

    /* 检查 WiFi 连接 */
    if (!wifi_is_connected()) {
        ai_ui_add_ai_message_async("[错误] WiFi 未连接，请检查网络");
        free(message);
        s_streaming = false;
        s_llm_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* 等待 SD 卡 I/O 完成，避免干扰 SDIO WiFi 通信 */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 优先使用调用者传入的学科参数，回退到 UI 全局状态 */
    extern int ai_llm_get_current_subject(void);
    const char *subjects[] = {"数学", "英语", "语文", "物理", "化学"};
    int subj_idx = ai_llm_get_current_subject();
    if (subj_idx >= 0 && subj_idx < 5) {
        strncpy(subject, subjects[subj_idx], sizeof(subject) - 1);
    }

    char *body = build_request_body(message, subject);
    free(message);
    if (!body) {
        ai_ui_add_ai_message_async("[系统错误] 构造请求失败");
        s_streaming = false;
        s_llm_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t config = {
        .url = LLM_DEFAULT_BASE_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .timeout_ms = LLM_TIMEOUT_MS,
        .buffer_size = LLM_BUF_SIZE,
        .buffer_size_tx = LLM_BUF_SIZE,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,  /* 使用 ESP-IDF 内置 CA 证书包 */
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ai_ui_add_ai_message_async("[系统错误] HTTP 客户端初始化失败");
        free(body);
        s_streaming = false;
        s_llm_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* 设置请求头 */
    char auth_header[160];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_api_key);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "text/event-stream");

    /* 设置请求体 */
    esp_http_client_set_post_field(client, body, strlen(body));

    ESP_LOGI(TAG, "Sending LLM request...");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d", status);
        if (status != 200) {
            ai_ui_append_ai_chunk_async("", true);  /* 结束流式 */
            
            /* 读取错误响应体 */
            char err_body[512] = {0};
            int read_len = esp_http_client_read_response(client, err_body, sizeof(err_body) - 1);
            if (read_len > 0) {
                err_body[read_len] = '\0';
                ESP_LOGE(TAG, "API error response: %s", err_body);
            }
            
            char err_msg[128];
            if (status == 401) {
                snprintf(err_msg, sizeof(err_msg), "[API错误] HTTP %d - API Key 无效或已过期", status);
            } else if (status == 429) {
                snprintf(err_msg, sizeof(err_msg), "[API错误] HTTP %d - 请求过于频繁，请稍后再试", status);
            } else if (status >= 500) {
                snprintf(err_msg, sizeof(err_msg), "[API错误] HTTP %d - 服务器错误，请稍后再试", status);
            } else {
                snprintf(err_msg, sizeof(err_msg), "[API错误] HTTP %d", status);
            }
            ai_ui_add_ai_message_async(err_msg);
        } else {
            /* 正常结束，发送结束标记 */
            ai_ui_append_ai_chunk_async("", true);
            /* 成就接线: AI 问答成功一次 → +经验 (投递到 LVGL 线程执行) */
            ai_ui_reward_ai_chat();
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        ai_ui_append_ai_chunk_async("", true);
        
        if (err == ESP_ERR_HTTP_CONNECT) {
            ai_ui_add_ai_message_async("[网络错误] 无法连接到服务器，请检查网络");
        } else if (err == ESP_ERR_HTTP_FETCH_HEADER) {
            ai_ui_add_ai_message_async("[网络错误] DNS 解析失败或服务器无响应");
        } else if (err == ESP_ERR_INVALID_STATE) {
            ai_ui_add_ai_message_async("[网络错误] TLS/SSL 证书验证失败");
        } else {
            ai_ui_add_ai_message_async("[网络错误] 请求失败，请检查网络连接");
        }
    }

    esp_http_client_cleanup(client);
    free(body);
    s_streaming = false;
    s_llm_task = NULL;
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════
   公共接口
   ═══════════════════════════════════════════════ */

/* 启动一次 LLM 流式请求任务(不含用户态留存/计数复位, 供首发与自动重试共用) */
static bool start_llm_request(const char *message)
{
    if (s_streaming) { ESP_LOGW(TAG, "Already streaming, please wait"); return false; }
    char *msg_copy = strdup(message);
    if (!msg_copy) return false;
    s_streaming = true;
    BaseType_t ret = xTaskCreate(llm_task, "llm_task", LLM_TASK_STACK_SIZE,
                                 msg_copy, LLM_TASK_PRIORITY, &s_llm_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create llm_task");
        free(msg_copy);
        s_streaming = false;
        return false;
    }
    return true;
}

void ai_llm_init(void)
{
    if (s_initialized) return;

    /* Key 来源优先级: sdkconfig 编译期 key(非空) > NVS(设置界面输入)。
     * 原因: 本工程把已验证可用的 key 烧进 sdkconfig, 应作为权威值, 避免 NVS 里
     * 旧的/带尾部空白的 key 造成 401。生产环境若想让 UI 输入生效, 把
     * CONFIG_SMARTBOX_LLM_API_KEY 留空即可自动回退到 NVS。 */
    const char *cfg_key = CONFIG_SMARTBOX_LLM_API_KEY;
    s_api_key[0] = '\0';
    if (cfg_key && cfg_key[0]) {
        strncpy(s_api_key, cfg_key, sizeof(s_api_key) - 1);
        s_api_key[sizeof(s_api_key) - 1] = '\0';
    } else {
        size_t key_len = sizeof(s_api_key);
        if (storage_load_llm_key(s_api_key, &key_len) != ESP_OK) {
            s_api_key[0] = '\0';
        }
    }

    /* 清理尾部空白/换行(UI 输入或 config 拼接常见): "Bearer <key>\n" 会被服务器判 401 */
    for (int i = (int)strlen(s_api_key) - 1;
         i >= 0 && (s_api_key[i] == '\r' || s_api_key[i] == '\n' ||
                    s_api_key[i] == ' '  || s_api_key[i] == '\t'); i--) {
        s_api_key[i] = '\0';
    }

    if (s_api_key[0] == '\0') {
        ESP_LOGW(TAG, "LLM API Key not configured, AI function disabled");
    } else {
        ESP_LOGI(TAG, "LLM API Key ready (%zu chars, src=%s)",
                 strlen(s_api_key), (cfg_key && cfg_key[0]) ? "sdkconfig" : "NVS");
    }

    s_initialized = true;
}

bool ai_llm_send_message(const char *message, const char *subject)
{
    (void)subject;  /* 学科由 llm_task 内部经 ai_llm_get_current_subject() 取, 此参数保留兼容 */
    if (!s_initialized) {
        ESP_LOGW(TAG, "LLM not initialized");
        return false;
    }
    if (s_api_key[0] == '\0') {
        ai_ui_add_ai_message_async("[提示] 请先在设置中配置 AI API Key");
        return false;
    }
    if (s_streaming) {
        ESP_LOGW(TAG, "Already streaming, please wait");
        return false;
    }

    return start_llm_request(message);
}

bool ai_llm_is_streaming(void)
{
    return s_streaming;
}

/* ═══════════════════════════════════════════════
   网络测试
   ═══════════════════════════════════════════════ */

static void test_network_task(void *arg)
{
    (void)arg;

    /* 先检查 WiFi 连接状态 */
    ai_ui_add_ai_message_async("[网络测试] 检查 WiFi 状态...");
    
    if (!wifi_is_connected()) {
        ai_ui_add_ai_message_async("[网络测试] ✗ WiFi 未连接！");
        ai_ui_add_ai_message_async("[网络测试] 建议: 返回首页检查 WiFi 状态");
        vTaskDelete(NULL);
        return;
    }
    
    ai_ui_add_ai_message_async("[网络测试] ✓ WiFi 已连接");
    
    /* 等待 SD 卡 I/O 完成，避免干扰 SDIO */
    vTaskDelay(pdMS_TO_TICKS(1000));

    ai_ui_add_ai_message_async("[网络测试] 正在测试 HTTP 连接...");

    /* 测试 1: HTTP GET 百度 */
    esp_http_client_config_t config = {
        .url = "http://www.baidu.com",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .buffer_size = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ai_ui_add_ai_message_async("[网络测试] HTTP 客户端初始化失败");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Testing HTTP connection to baidu.com...");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int content_len = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP test OK: status=%d, len=%d", status, content_len);

        char result[128];
        snprintf(result, sizeof(result),
                 "[网络测试] ✓ 百度连接正常 (HTTP %d, %d bytes)",
                 status, content_len);
        ai_ui_add_ai_message_async(result);
    } else {
        ESP_LOGE(TAG, "HTTP test failed: %s", esp_err_to_name(err));

        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg),
                 "[网络测试] ✗ 百度连接失败: %s", esp_err_to_name(err));
        ai_ui_add_ai_message_async(err_msg);

        /* 进一步诊断 */
        if (err == ESP_ERR_HTTP_CONNECT) {
            ai_ui_add_ai_message_async("[网络测试] 建议: 检查 WiFi 是否已连接");
        } else if (err == ESP_ERR_HTTP_FETCH_HEADER) {
            ai_ui_add_ai_message_async("[网络测试] 建议: DNS 解析失败，检查网络");
        }
    }

    esp_http_client_cleanup(client);

    /* 测试 2: HTTPS GET 豆包 API */
    ai_ui_add_ai_message_async("[网络测试] 正在测试 HTTPS 连接...");

    esp_http_client_config_t config2 = {
        .url = "https://ark.cn-beijing.volces.com",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .buffer_size = 1024,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,  /* 使用 ESP-IDF 内置 CA 证书包 */
    };

    client = esp_http_client_init(&config2);
    if (client) {
        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "HTTPS test OK: status=%d", status);
            char result[128];
            snprintf(result, sizeof(result),
                     "[网络测试] ✓ 豆包 API 连接正常 (HTTP %d)", status);
            ai_ui_add_ai_message_async(result);
        } else {
            ESP_LOGE(TAG, "HTTPS test failed: %s", esp_err_to_name(err));
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg),
                     "[网络测试] ✗ 豆包 API 连接失败: %s", esp_err_to_name(err));
            ai_ui_add_ai_message_async(err_msg);
        }
        esp_http_client_cleanup(client);
    }

    vTaskDelete(NULL);
}

void ai_llm_test_network(void)
{
    xTaskCreate(test_network_task, "net_test", 6144, NULL, 5, NULL);
}