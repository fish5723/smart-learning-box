/**
 * @file http_manager.c
 * @brief 共享 HTTP 客户端实现
 *
 * 特性:
 *   - 每次请求创建独立 esp_http_client handle, 用完销毁
 *   - Header (Authorization/Content-Type/Custom) 由本模块统一拼接
 *   - 内部动态缓冲: 支持 Content-Length 和 Transfer-Encoding: chunked
 *   - 完整响应可通过 http_manager_get_body() 获取
 *   - 兼容 caller 提供的 resp_buf (固定缓冲)
 */
#include "http_manager.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "HTTP_MGR";

#define HTTP_DEFAULT_TIMEOUT_MS   30000
#define HTTP_MIN_BUF_SIZE         4096
#define HTTP_MAX_HEADER_LEN       512

/* 响应体内部缓冲 — 初始大小, 不足时 2x 扩容 */
#define RESP_INIT_SIZE            4096
#define RESP_MAX_SIZE             (128 * 1024)  /* 128KB 硬上限 */

struct http_handle_t {
    esp_http_client_handle_t client;
    volatile bool            cancelled;

    /* 内部响应缓冲 (动态扩容) */
    uint8_t *resp_data;
    size_t   resp_size;      /* 分配大小 */
    size_t   resp_len;       /* 实际写入字节数 */
};

/* ═══════════════════════════════════════════════
   Handle 生命周期
   ═══════════════════════════════════════════════ */

http_handle_t* http_manager_create(void)
{
    http_handle_t *h = calloc(1, sizeof(http_handle_t));
    if (!h) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    h->cancelled = false;
    ESP_LOGD(TAG, "Handle created: %p", (void*)h);
    return h;
}

int http_manager_destroy(http_handle_t *h)
{
    if (!h) return 0;
    if (h->client) {
        esp_http_client_cleanup(h->client);
        h->client = NULL;
    }
    if (h->resp_data) {
        free(h->resp_data);
        h->resp_data = NULL;
    }
    free(h);
    return 0;
}

int http_manager_cancel(http_handle_t *h)
{
    if (!h) return 0;
    h->cancelled = true;
    ESP_LOGI(TAG, "Request cancelled");
    return 0;
}

/* ═══════════════════════════════════════════════
   响应体获取
   ═══════════════════════════════════════════════ */

const char* http_manager_get_body(http_handle_t *h)
{
    if (!h || !h->resp_data || h->resp_len == 0) return NULL;
    return (const char *)h->resp_data;
}

size_t http_manager_get_body_len(http_handle_t *h)
{
    if (!h) return 0;
    return h->resp_len;
}

/* ═══════════════════════════════════════════════
   Internal: HTTP Event Handler — 收集响应体
   ═══════════════════════════════════════════════ */

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    http_handle_t *h = (http_handle_t *)evt->user_data;
    if (!h) return ESP_FAIL;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA: {
        /* 非 chunked: 仅在没有 resp_data 时走 event handler
           (chunked 由 _read_response 的 esp_http_client_read 解码) */
        if (esp_http_client_is_chunked_response(evt->client)) {
            /* chunked: 仍由 _read_response 处理 */
            break;
        }

        /* 确保缓冲足够 (留 1 字节给 '\0') */
        size_t new_len = h->resp_len + evt->data_len;
        if (new_len >= h->resp_size) {
            size_t new_size = h->resp_size > 0 ? h->resp_size * 2 : RESP_INIT_SIZE;
            while (new_size < new_len + 1 && new_size < RESP_MAX_SIZE) {
                new_size *= 2;
            }
            if (new_size > RESP_MAX_SIZE) {
                new_size = RESP_MAX_SIZE;
            }
            if (new_size <= h->resp_size) {
                ESP_LOGW(TAG, "Response exceeds max size, truncating");
                break;
            }
            uint8_t *new_buf = realloc(h->resp_data, new_size);
            if (!new_buf) {
                ESP_LOGW(TAG, "Buffer realloc failed, dropping data");
                break;
            }
            h->resp_data = new_buf;
            h->resp_size = new_size;
        }

        memcpy(h->resp_data + h->resp_len, evt->data, evt->data_len);
        h->resp_len = new_len;
        h->resp_data[h->resp_len] = '\0';
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
   Internal: 构建 esp_http_client_config_t
   ═══════════════════════════════════════════════ */

static void _setup_client(http_handle_t *h, http_req_t *req)
{
    /* TX buffer 至少 4096, 大 body 时匹配 body_len */
    int tx_buf = HTTP_MIN_BUF_SIZE;
    if (req->body_len > tx_buf) tx_buf = req->body_len;

    esp_http_client_config_t cfg = {
        .url               = req->url,
        .method            = (req->method && strcmp(req->method, "GET") == 0)
                             ? HTTP_METHOD_GET : HTTP_METHOD_POST,
        .timeout_ms        = req->timeout_ms > 0 ? req->timeout_ms : HTTP_DEFAULT_TIMEOUT_MS,
        .buffer_size       = HTTP_MIN_BUF_SIZE,
        .buffer_size_tx    = tx_buf,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler     = _http_event_handler,
        .user_data         = h,
    };
    h->client = esp_http_client_init(&cfg);
}

static void _set_headers(http_handle_t *h, http_req_t *req)
{
    esp_err_t ret;

    if (req->api_key) {
        char auth[256];
        snprintf(auth, sizeof(auth), "Bearer %s", req->api_key);

        /* 诊断: 打印 key 前4+后4, 中间隐藏 */
        size_t klen = strlen(req->api_key);
        if (klen > 10) {
            ESP_LOGI(TAG, "Auth: Bearer %.4s...%.4s (%zu chars)",
                     req->api_key, req->api_key + klen - 4, klen);
        } else if (klen > 0) {
            ESP_LOGI(TAG, "Auth: Bearer **** (%zu chars)", klen);
        } else {
            ESP_LOGW(TAG, "Auth: api_key is empty string!");
        }

        ret = esp_http_client_set_header(h->client, "Authorization", auth);
        ESP_LOGI(TAG, "  set_header(Authorization) → %d", ret);
    } else {
        ESP_LOGW(TAG, "Auth: api_key is NULL — no Authorization header set!");
    }

    const char *ct = req->content_type ? req->content_type : "application/json";
    ret = esp_http_client_set_header(h->client, "Content-Type", ct);
    ESP_LOGI(TAG, "  set_header(Content-Type, %s) → %d", ct, ret);

    if (req->extra_headers) {
        char buf[HTTP_MAX_HEADER_LEN];
        strncpy(buf, req->extra_headers, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *line = strtok(buf, "\r\n");
        while (line) {
            while (*line == ' ' || *line == '\t') line++;
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *val = colon + 1;
                while (*val == ' ') val++;
                ret = esp_http_client_set_header(h->client, line, val);
                ESP_LOGI(TAG, "  set_header(%s, %s) → %d", line, val, ret);
            }
            line = strtok(NULL, "\r\n");
        }
    }
}

/* ═══════════════════════════════════════════════
   Internal: 响应体动态读取
   ═══════════════════════════════════════════════ */

static int _read_response(http_handle_t *h)
{
    /* 释放旧缓冲 */
    if (h->resp_data) {
        free(h->resp_data);
        h->resp_data = NULL;
    }
    h->resp_size = RESP_INIT_SIZE;
    h->resp_len  = 0;

    h->resp_data = malloc(h->resp_size);
    if (!h->resp_data) {
        ESP_LOGE(TAG, "Failed to allocate response buffer (%u bytes)",
                 (unsigned)h->resp_size);
        return -1;
    }

    int total   = 0;
    int chunk   = 0;
    int retries = 0;  /* EAGAIN 重试计数 */

    while (1) {
        /* 扩容检查: 留 1 字节给 '\0' */
        if (total >= (int)(h->resp_size - 1)) {
            size_t new_size = h->resp_size * 2;
            if (new_size > RESP_MAX_SIZE) {
                ESP_LOGW(TAG, "Response exceeds max size (%u), truncating",
                         (unsigned)RESP_MAX_SIZE);
                break;
            }
            uint8_t *new_buf = realloc(h->resp_data, new_size);
            if (!new_buf) {
                ESP_LOGW(TAG, "Buffer realloc failed, truncating at %d bytes", total);
                break;
            }
            h->resp_data = new_buf;
            h->resp_size = new_size;
            ESP_LOGD(TAG, "Response buffer expanded to %u bytes", (unsigned)new_size);
        }

        chunk = esp_http_client_read(h->client,
                     (char *)(h->resp_data + total),
                     h->resp_size - total - 1);

        if (chunk > 0) {
            total += chunk;
            retries = 0;
            esp_task_wdt_reset();
        } else if (chunk == 0) {
            /* Transfer-Encoding: chunked 结束 — 正常终止 */
            ESP_LOGD(TAG, "Chunked transfer complete (total=%d)", total);
            break;
        } else {
            /* chunk < 0 — 错误 */
            if (chunk == -ESP_ERR_HTTP_EAGAIN && retries < 3) {
                /* EAGAIN: 等待更多数据 (非 chunked 连接) */
                retries++;
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            /* -1 通常意味着非 chunked 响应已读完 (服务器关闭连接)
             * 如果有数据则视为成功, 无数据则记录 */
            if (total > 0 && chunk == -1) {
                ESP_LOGD(TAG, "Connection closed with data (total=%d, err=%d)",
                         total, chunk);
                break;
            }
            ESP_LOGW(TAG, "Read error: %d at total=%d (retries=%d)",
                     chunk, total, retries);
            break;
        }
    }

    h->resp_data[total] = '\0';
    h->resp_len = total;
    return total;
}

/* ═══════════════════════════════════════════════
   Internal: 执行请求
   ═══════════════════════════════════════════════ */

static int _execute(http_handle_t *h, http_req_t *req, int *status_code)
{
    /* ── 设置 POST body ── */
    if (req->body && req->body_len > 0) {
        esp_err_t set_ret = esp_http_client_set_post_field(h->client,
                                    req->body, req->body_len);
        if (set_ret != ESP_OK) {
            ESP_LOGE(TAG, "set_post_field failed: %s (0x%x)",
                     esp_err_to_name(set_ret), set_ret);
        }
        ESP_LOGI(TAG, "→ %s %s (timeout=%dms, body=%d bytes, tx_buf_ok=%d)",
                 req->method ? req->method : "POST",
                 req->url,
                 req->timeout_ms > 0 ? req->timeout_ms : HTTP_DEFAULT_TIMEOUT_MS,
                 req->body_len,
                 (set_ret == ESP_OK));
    } else {
        ESP_LOGI(TAG, "→ %s %s (timeout=%dms)",
                 req->method ? req->method : "GET",
                 req->url,
                 req->timeout_ms > 0 ? req->timeout_ms : HTTP_DEFAULT_TIMEOUT_MS);
    }

    /* ── 确保响应缓冲已分配 (供 event handler 写入) ── */
    if (!h->resp_data) {
        h->resp_size = RESP_INIT_SIZE;
        h->resp_len  = 0;
        h->resp_data = malloc(h->resp_size);
        if (h->resp_data) {
            h->resp_data[0] = '\0';
        }
    }

    /* ── 执行 ── */
    esp_task_wdt_reset();
    esp_err_t err = esp_http_client_perform(h->client);

    if (h->cancelled) {
        return ERR_HTTP_CANCELLED;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP perform failed: %s (0x%x)", esp_err_to_name(err), err);
        if (err == ESP_ERR_HTTP_CONNECT)       return ERR_HTTP_CONNECT;
        if (err == ESP_ERR_HTTP_EAGAIN)        return ERR_HTTP_TIMEOUT;
        return ERR_HTTP_CONNECT;
    }

    /* ── 响应元数据 ── */
    int status         = esp_http_client_get_status_code(h->client);
    int content_length = esp_http_client_get_content_length(h->client);
    bool is_chunked    = esp_http_client_is_chunked_response(h->client);

    if (status_code) *status_code = status;

    ESP_LOGI(TAG, "← HTTP %d | Content-Length=%d | chunked=%d",
             status, content_length, is_chunked);

    /* ── 读取响应体 ──
       non-chunked: event handler 已在 perform 期间收集 → h->resp_len > 0
       chunked:     event handler 不处理 → h->resp_len == 0 → 回退到 _read_response */
    int total;
    if (h->resp_len > 0) {
        /* 非 chunked: event handler 已收集完整响应体 */
        total = (int)h->resp_len;
        ESP_LOGD(TAG, "Response collected via event handler: %d bytes", total);
    } else {
        /* Chunked 或无数据: 使用 esp_http_client_read 回退 */
        total = _read_response(h);
    }

    ESP_LOGI(TAG, "Read complete: %d bytes (chunked=%d, Content-Length=%d)",
             total, is_chunked, content_length);

    /* ── 写入 caller 缓冲 (兼容旧接口) ── */
    if (req->resp_buf && req->resp_buf_sz > 0 && req->resp_len) {
        size_t copy_len = (size_t)total < req->resp_buf_sz - 1
                          ? (size_t)total : req->resp_buf_sz - 1;
        if (copy_len > 0) {
            memcpy(req->resp_buf, h->resp_data, copy_len);
        }
        req->resp_buf[copy_len] = '\0';
        *req->resp_len = copy_len;

        if ((size_t)total >= req->resp_buf_sz) {
            ESP_LOGW(TAG, "Response truncated: %d → %u bytes (use http_manager_get_body for full)",
                     total, (unsigned)copy_len);
        }
    }

    /* ── 预览响应内容 ── */
    if (h->resp_len > 0 && h->resp_len <= 1024) {
        ESP_LOGI(TAG, "Response body: %s", (const char *)h->resp_data);
    } else if (h->resp_len > 1024) {
        ESP_LOGI(TAG, "Response body (first 512): %.512s", (const char *)h->resp_data);
    } else {
        ESP_LOGW(TAG, "Response body is EMPTY (0 bytes)");
    }

    return 0;
}

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

int http_manager_post(http_handle_t *h, http_req_t *req, int *status_code)
{
    if (!h || !req || !req->url) return ERR_HTTP_CONNECT;

    h->cancelled = false;

    /* 释放上一次请求的内部缓冲 */
    if (h->resp_data) {
        free(h->resp_data);
        h->resp_data = NULL;
        h->resp_size = 0;
        h->resp_len  = 0;
    }
    if (h->client) {
        esp_http_client_cleanup(h->client);
        h->client = NULL;
    }

    _setup_client(h, req);
    _set_headers(h, req);
    return _execute(h, req, status_code);
}

int http_manager_get(http_handle_t *h, http_req_t *req, int *status_code)
{
    if (!h || !req || !req->url) return ERR_HTTP_CONNECT;

    h->cancelled = false;

    if (h->resp_data) {
        free(h->resp_data);
        h->resp_data = NULL;
        h->resp_size = 0;
        h->resp_len  = 0;
    }
    if (h->client) {
        esp_http_client_cleanup(h->client);
        h->client = NULL;
    }

    _setup_client(h, req);
    _set_headers(h, req);
    return _execute(h, req, status_code);
}

/* ═══════════════════════════════════════════════
   Buffer 池 (PSRAM)
   ═══════════════════════════════════════════════ */

#define HTTP_BUF_DEFAULT_SIZE 4096

void* http_buf_alloc(void)
{
    return calloc(1, HTTP_BUF_DEFAULT_SIZE);
}

void http_buf_free(void *buf)
{
    if (buf) free(buf);
}
