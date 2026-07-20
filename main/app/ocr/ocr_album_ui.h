/**
 * @file ocr_album_ui.h
 * @brief 相册导入 — 从 /sdcard/photos 选择一张 JPEG 交给 OCR 流水线
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 用户选中一张照片时的回调
 * @param jpg_path  选中图片的完整路径 (如 "/sdcard/photos/20260704_153022.jpg")
 *                  仅在回调期间有效, 需要保留请自行复制。
 */
typedef void (*ocr_album_pick_cb_t)(const char *jpg_path);

/**
 * @brief 相册弹窗关闭且【未选中】任何照片时的回调 (用户点关闭 / 无照片 / SD 异常)
 *
 * 选中照片走 on_pick, 不触发本回调。调用方据此恢复被暂停的摄像头预览。
 */
typedef void (*ocr_album_dismiss_cb_t)(void);

/**
 * @brief 初始化相册选择器 (延迟创建)
 */
void ocr_album_ui_init(void);

/**
 * @brief 显示相册选择器弹窗
 *
 * 扫描 /sdcard/photos 下 JPEG 文件, 以网格列出。用户点击某张 → 关闭弹窗并回调 on_pick。
 *
 * @param on_pick     选中回调 (可为 NULL, 仅浏览)
 * @param on_dismiss  未选中而关闭的回调 (可为 NULL); 用于恢复调用方暂停的预览
 */
void ocr_album_ui_show(ocr_album_pick_cb_t on_pick,
                       ocr_album_dismiss_cb_t on_dismiss);

#ifdef __cplusplus
}
#endif
