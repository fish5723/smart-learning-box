/**
 * @file ocr.h
 * @brief 拍照解题 — UI-only 接口（后端/相机/API 已移除）
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OCR 模块初始化（仅初始化 UI）
 */
void ocr_init(void);

/**
 * @brief 显示 OCR 页面
 */
void ocr_show(void);

/**
 * @brief 崩溃恢复: 清理上次 OCR 中断留下的 pending 文件（无操作桩）
 */
void ocr_recover_pending(void);

#ifdef __cplusplus
}
#endif