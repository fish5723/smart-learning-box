/**
 * @file ocr_ui.h
 * @brief OCR 拍照解题 UI 层接口
 */

#pragma once

#include "lvgl.h"
#include "common/vision_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 OCR 页面 UI
 */
void ocr_ui_init(void);

/**
 * @brief 显示 OCR 页面
 */
void ocr_ui_show(void);

/**
 * @brief 隐藏 OCR 页面
 */
void ocr_ui_hide(void);

/**
 * @brief 更新摄像头状态文字
 * @param text 状态文字, 如 "摄像头已连接"
 */
void ocr_ui_set_status(const char *text);

/**
 * @brief 设置摄像头连接状态 (带颜色指示)
 * @param connected true=已连接(绿色), false=未连接(红色)
 */
void ocr_ui_set_camera_connected(bool connected);

/**
 * @brief 更新 OCR 识别结果
 * @param text 识别到的题目文字
 */
void ocr_ui_set_question(const char *text);

/**
 * @brief 更新 AI 讲解内容
 * @param text AI 讲解文字
 */
void ocr_ui_set_explain(const char *text);

/**
 * @brief 追加 AI 讲解内容 (用于深入讲解)
 * @param text 追加的讲解文字
 */
void ocr_ui_append_explain(const char *text);

/**
 * @brief 添加知识点标签
 * @param text 标签文字
 */
void ocr_ui_add_tag(const char *text);

/**
 * @brief 清空 AI 讲解内容
 */
void ocr_ui_clear_explain(void);

/**
 * @brief 清空所有知识点标签
 */
void ocr_ui_clear_tags(void);

/**
 * @brief 滚动讲解卡片到底部
 * @note  必须在 LVGL UI 线程调用
 */
void ocr_ui_scroll_explain_to_bottom(void);

/**
 * @brief 显示结果卡片 (隐藏空状态)
 * @note  必须在 LVGL UI 线程调用
 */
void ocr_ui_show_result(void);

/**
 * @brief 在预览区显示摄像头快照 (RGB565 格式)
 *
 * 将拍照得到的 RGB565 原始帧渲染到左侧预览区。
 * 内部通过 lv_async_call 调度，可在任意线程调用。
 *
 * @param frame  RGB565 图像帧 (内部会复制，调用方仍需自行释放)
 */
void ocr_ui_show_camera_frame(const vision_blob_t *frame);

/**
 * @brief 清除预览画面，恢复占位符
 */
void ocr_ui_clear_preview(void);

/**
 * @brief 设置答案文字 (答案高亮区域)
 */
void ocr_ui_set_answer(const char *text);

/** @brief 显示/隐藏答案区域 */
void ocr_ui_show_answer(void);
void ocr_ui_hide_answer(void);

/** @brief 显示/隐藏标签卡片 */
void ocr_ui_show_tags(void);
void ocr_ui_hide_tags(void);

#ifdef __cplusplus
}
#endif