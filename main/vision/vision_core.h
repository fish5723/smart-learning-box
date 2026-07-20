/**
 * @file vision_core.h
 * @brief Vision 平台层 — 纯任务调度, 零业务逻辑
 *
 * 职责:
 * 1. 单任务调度 (一次只允许一个 Vision 任务)
 * 2. 状态机驱动 (IDLE→PREPARE→CAPTURE→UPLOAD→PROCESS→DISPLAY→CLEANUP)
 * 3. 每阶段独立超时保护
 * 4. 任意阶段可 Cancel
 * 5. 统一回调 (on_state / on_progress / on_result / on_error, 全带 user_data)
 *
 * 不包含任何 OCR/翻译/QA 业务代码 — 这些由插件通过 vision_task_config_t 注入。
 */
#pragma once

#include "common/error_code.h"
#include "common/vision_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════
   配置宏
   ═══════════════════════════════════════════════ */
#define CONFIG_VISION_TASK_STACK    8192
#define CONFIG_VISION_TASK_PRIO     4

/* ═══════════════════════════════════════════════
   状态枚举
   ═══════════════════════════════════════════════ */
typedef enum {
    VISION_IDLE,
    VISION_PREPARE,
    VISION_CAPTURE,
    VISION_UPLOAD,
    VISION_PROCESS,
    VISION_DISPLAY,
    VISION_CLEANUP,
    VISION_ERROR,
    VISION_CANCELLED,
} vision_state_t;

/* ═══════════════════════════════════════════════
   任务配置 — 业务插件填充此结构体
   ═══════════════════════════════════════════════ */
typedef int (*vision_hook_fn)(void *user_data, vision_result_t *io);

typedef struct {
    const char        *name;             /* "OCR" / "TRANSLATE" / "QA" */
    vision_endpoint_t  endpoint;
    vision_hook_fn     fn_capture;
    vision_hook_fn     fn_upload;
    vision_hook_fn     fn_process;
    vision_hook_fn     fn_cleanup;          /* CLEANUP 阶段释放资源, 可 NULL */
    int                capture_timeout_ms;   /* 0=default 30000 */
    int                upload_timeout_ms;    /* 0=default 60000 */
    int                process_timeout_ms;   /* 0=default 30000 */
} vision_task_config_t;

/* ═══════════════════════════════════════════════
   回调 (全部带 void *user_data)
   ═══════════════════════════════════════════════ */
typedef struct {
    void (*on_state)(vision_state_t state, void *user_data);
    void (*on_progress)(int percent, void *user_data);
    void (*on_result)(const vision_result_t *result, void *user_data);
    void (*on_error)(int error_code, const char *msg, void *user_data);
} vision_callback_t;

/* ═══════════════════════════════════════════════
   API
   ═══════════════════════════════════════════════ */

int            vision_core_init(void);
void           vision_core_deinit(void);
int            vision_start(const vision_task_config_t *cfg,
                            const vision_callback_t *cb, void *user_data);
int            vision_cancel(void);
bool           vision_is_busy(void);
vision_state_t vision_get_state(void);

#ifdef __cplusplus
}
#endif
