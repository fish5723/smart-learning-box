/**
 * @file camera_manager.h
 * @brief 相机管理层 — 唯一 Camera Owner
 *
 * 职责:
 * - Camera 生命周期管理
 * - 实时 Preview 帧输出 (20~30fps callback)
 * - Freeze/Resume 当前帧
 * - 单帧 Capture + JPEG 编码 (OCR/识物等模块使用)
 *
 * Preview 模式:
 *   camera_manager_preview_start(cb) → 持续回调帧
 *   camera_manager_preview_freeze()  → 冻结当前帧 (OCR 用)
 *   camera_manager_preview_resume()  → 恢复实时回调
 *   camera_manager_preview_stop()    → 停止预览
 *
 * Capture 模式 (兼容旧接口):
 *   camera_manager_capture() → 单帧 RGB565
 *   camera_manager_encode_jpeg() → JPEG 硬件编码
 */
#pragma once

#include "common/error_code.h"
#include "common/vision_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════
   Preview Callback
   ═══════════════════════════════════════════════ */

/**
 * @brief 预览帧回调 (在 Camera Manager 的 preview task 中调用)
 *
 * @param frame     当前预览帧 (指针仅在回调内有效，UI 需自行拷贝)
 * @param user_data 注册时传入的透传指针
 */
typedef void (*camera_preview_cb_t)(const vision_blob_t *frame, void *user_data);

/* ═══════════════════════════════════════════════
   Init / Deinit
   ═══════════════════════════════════════════════ */

int  camera_manager_init(void);
void camera_manager_deinit(void);
bool camera_manager_is_ready(void);

/* ═══════════════════════════════════════════════
   Preview API (实时预览 + 冻结)
   ═══════════════════════════════════════════════ */

/**
 * @brief 启动实时预览
 * @param cb        每帧回调
 * @param user_data 透传指针
 * @return ERR_OK 或错误码
 */
int  camera_manager_preview_start(camera_preview_cb_t cb, void *user_data);

/**
 * @brief 停止实时预览 (释放所有 buffer, 停止 CSI 流)
 * @return ERR_OK 或错误码
 */
int  camera_manager_preview_stop(void);

/**
 * @brief 冻结当前预览帧 (不再投递回调, 保留帧供 OCR 使用)
 * @return ERR_OK 或错误码
 */
int  camera_manager_preview_freeze(void);

/**
 * @brief 恢复实时预览 (释放冻结帧, 恢复回调投递)
 * @return ERR_OK 或错误码
 */
int  camera_manager_preview_resume(void);

/**
 * @brief 获取冻结帧指针 (只读, 在 resume 前有效)
 * @return 冻结帧指针, 无冻结帧时返回 NULL
 */
const vision_blob_t *camera_manager_get_frozen_frame(void);

/**
 * @brief 预览是否正在运行
 */
bool camera_manager_is_preview_active(void);

/* ═══════════════════════════════════════════════
   Capture API (单帧模式, 向后兼容)
   ═══════════════════════════════════════════════ */

int  camera_manager_capture(vision_blob_t *rgb565);
int  camera_manager_encode_jpeg(const vision_blob_t *rgb565, vision_blob_t *jpeg);
void camera_manager_blob_free(vision_blob_t *blob);

#ifdef __cplusplus
}
#endif
