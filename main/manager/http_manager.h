/**
 * @file http_manager.h
 * @brief 共享 HTTP 客户端 — 按请求创建 Handle, 统一 Header, 支持 Cancel
 *
 * 不是单例: 每次请求创建独立 http_handle_t, 用完销毁。
 * ESP-IDF 的 esp_http_client 不保证线程安全, 独立 handle 避免锁竞争。
 * 业务层不拼接 HTTP Header, 由 http_manager 根据 req 字段统一设置。
 *
 * 响应体:
 *   - 内部动态缓冲, 支持 Content-Length 和 Transfer-Encoding: chunked
 *   - 写入 caller 提供的 resp_buf (截断时日志告警)
 *   - 完整响应可通过 http_manager_get_body() 获取 (valid until destroy)
 */
#pragma once

#include "common/error_code.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct http_handle_t http_handle_t;

/* ═══════════════════════════════════════════════
   Request 配置
   ═══════════════════════════════════════════════ */
typedef struct {
    const char *url;
    const char *method;           /* "GET" / "POST" */
    const char *body;             /* POST body, NULL for GET */
    int         body_len;
    int         timeout_ms;

    /* 统一 Header (http_manager 内部拼接) */
    const char *api_key;          /* NULL=无 Authorization */
    const char *content_type;     /* NULL=默认 application/json */
    const char *extra_headers;    /* NULL 或 "Key: Val\r\n..." */

    /* 响应 (输出) — 内部动态缓冲; 此处为 caller 提供的固定缓冲 (兼容) */
    uint8_t    *resp_buf;         /* caller 缓冲, 可 NULL */
    size_t      resp_buf_sz;      /* caller 缓冲大小 */
    size_t     *resp_len;         /* 实际写入字节数 */
} http_req_t;

/* ═══════════════════════════════════════════════
   Handle 生命周期
   ═══════════════════════════════════════════════ */

http_handle_t* http_manager_create(void);
int            http_manager_destroy(http_handle_t *h);

/* ═══════════════════════════════════════════════
   请求执行 (同步阻塞)
   ═══════════════════════════════════════════════ */

int http_manager_post(http_handle_t *h, http_req_t *req, int *status_code);
int http_manager_get(http_handle_t *h, http_req_t *req, int *status_code);

/* ═══════════════════════════════════════════════
   Cancel (配合 vision_cancel 终止飞行请求)
   ═══════════════════════════════════════════════ */

int http_manager_cancel(http_handle_t *h);

/* ═══════════════════════════════════════════════
   响应体获取 (valid until http_manager_destroy)
   ═══════════════════════════════════════════════ */

const char* http_manager_get_body(http_handle_t *h);
size_t      http_manager_get_body_len(http_handle_t *h);

/* ═══════════════════════════════════════════════
   Buffer 池 (跨 handle 复用, 减少 PSRAM malloc/free)
   ═══════════════════════════════════════════════ */

void* http_buf_alloc(void);
void  http_buf_free(void *buf);

#ifdef __cplusplus
}
#endif
