/**
 * @file vision_ocr.h
 * @brief OCR 拍照搜题插件 — 填充 vision_task_config_t, 调用 vision_start()
 */
#pragma once

#include "common/vision_types.h"
#include "vision/vision_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int  vision_ocr_init(void);
void vision_ocr_deinit(void);

/**
 * @brief 启动 OCR 拍照搜题 (两步流水线)
 *
 * Step 1 (fn_upload):  JPEG → vision_endpoint (Qwen3-VL)  → 识别文字
 * Step 2 (fn_process): 文字  → solver_endpoint (豆包)    → 解题 + 解析
 *
 * @param vision_endpoint  视觉识别 API (Qwen/Qwen3-VL-8B-Instruct)
 * @param solver_endpoint  解题推理 API (豆包 Doubao)
 * @param cb               回调 (on_state/on_result/on_error, 全部在 vision 线程调用)
 * @param user_data        透传
 * @return 0 成功 / ERR_VISION_BUSY
 */
int vision_ocr_start(const vision_endpoint_t *vision_endpoint,
                     const vision_endpoint_t *solver_endpoint,
                     const vision_callback_t *cb, void *user_data);

/**
 * @brief 启动 OCR 解题 — 直接使用外部导入的 JPEG (跳过相机拍照)
 *
 * 与 vision_ocr_start 复用同一流水线, 唯一区别: fn_capture 不再取相机冻结帧,
 * 而是使用此处预加载的 JPEG。upload/process/cleanup 阶段完全一致。
 *
 * @param jpeg_data  JPEG 图像数据 (内部会复制一份, 调用方仍需自行释放)
 * @param jpeg_len   JPEG 数据长度
 * @param vision_endpoint  视觉识别 API (Qwen/Qwen3-VL)
 * @param solver_endpoint  解题推理 API (豆包)
 * @param cb               回调 (与 vision_ocr_start 一致)
 * @param user_data        透传 (当前未使用, 保持接口一致)
 * @return 0 成功 / ERR_VISION_BUSY / ERR_VISION_CAPTURE_FAILED (数据非法/内存不足)
 */
int vision_ocr_start_from_jpeg(const uint8_t *jpeg_data, size_t jpeg_len,
                               const vision_endpoint_t *vision_endpoint,
                               const vision_endpoint_t *solver_endpoint,
                               const vision_callback_t *cb, void *user_data);

/**
 * @brief 从 OCR 任务的 user_data 中获取 JPEG 数据 (供 _on_ocr_result 使用)
 *
 * JPEG 在 _ocr_upload 之后不再被提前释放，生命周期延续到 _ocr_cleanup。
 * 在 on_result 回调期间调用这两个 accessor 即可安全读取 JPEG 数据。
 *
 * @param user_data  OCR 回调中的 user_data 参数
 * @return JPEG 数据指针 (只读) 或 NULL; JPEG 字节数 或 0
 */
const uint8_t *vision_ocr_get_jpeg_data(const void *user_data);
size_t vision_ocr_get_jpeg_size(const void *user_data);

/**
 * @brief 该任务的 JPEG 是否为外部导入 (而非相机拍摄)
 *
 * 导入的图片通常已存在于相册中, UI 可据此跳过重复保存到拍照历史。
 *
 * @param user_data  OCR 回调中的 user_data 参数
 * @return true=导入, false=相机拍摄
 */
bool vision_ocr_is_imported(const void *user_data);

#ifdef __cplusplus
}
#endif
