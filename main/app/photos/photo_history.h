/**
 * @file photo_history.h
 * @brief 拍照历史 — 每次 OCR 拍照自动保存 JPEG + 题目/答案到 SD 卡
 *
 * 目录结构:
 *   /sdcard/photos/
 *     ├── 20260704_153022.jpg   (JPEG 照片)
 *     └── 20260704_153022.txt   (题目 + 答案文本)
 *
 * 依赖:
 *   - SD 卡已挂载
 *   - <time.h> 用于时间戳 (若未同步则使用增量计数器)
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PHOTO_HISTORY_MAX_FILENAME  32
#define PHOTO_HISTORY_PREVIEW_LEN   128

/** @brief JPEG 探测结果 (用于校验 + 硬件解码可行性判断) */
typedef struct {
    bool     valid;          /* 结构完整: 有 SOI + SOF + EOI */
    bool     has_soi;        /* 起始 FF D8 */
    bool     has_eoi;        /* 结尾 FF D9 (完整未截断) */
    bool     has_sof;        /* 找到 SOF0/1/2 帧头 */
    int      width;          /* SOF 中的宽 */
    int      height;         /* SOF 中的高 */
    int      components;     /* 分量数 (3=YCbCr) */
    bool     hw_decodable;   /* P4 硬件 JPEG 解码器可处理 (宽高均 %16==0 且 >=64) */
    size_t   file_size;      /* 文件字节数 */
} jpeg_probe_t;

/**
 * @brief 探测一个 JPEG 文件的合法性与尺寸 (只读文件头/尾, 不整体解码)
 *
 * 读取 SOI(FF D8)、扫描 SOF 标记获取宽高/分量数、检查文件尾 EOI(FF D9)。
 * 用于导入相册前过滤损坏/截断/尺寸不适配硬件解码器的图片。
 * 全过程打印 ESP_LOGI 便于现场诊断。
 *
 * @param jpg_path  完整 POSIX 路径 (如 "/sdcard/photos/xxx.jpg")
 * @param[out] out  探测结果
 * @return ESP_OK 成功读取 (out.valid 表示是否合法); ESP_ERR_* 打开/读取失败
 */
esp_err_t photo_history_probe_jpeg(const char *jpg_path, jpeg_probe_t *out);

/** @brief 拍照条目摘要 (用于列表浏览) */
typedef struct {
    char   filename[PHOTO_HISTORY_MAX_FILENAME];  /* "20260704_153022" */
    char   timestamp[20];                          /* "2026-07-04 15:30:22" */
    size_t jpeg_size;                              /* JPEG 文件大小 (bytes) */
    char   question[PHOTO_HISTORY_PREVIEW_LEN];    /* 题目前 128 字符 */
} photo_entry_t;

/**
 * @brief 初始化拍照历史子系统
 *
 * 创建 /sdcard/photos/ 目录。
 *
 * @return ESP_OK 成功; ESP_ERR_INVALID_STATE SD 未挂载
 */
esp_err_t photo_history_init(void);

/**
 * @brief 保存一张照片及其 OCR 结果
 *
 * 写入两个文件: <timestamp>.jpg 和 <timestamp>.txt。
 * .txt 格式: 第 1 行为题目, 后续行为讲解内容。
 *
 * @param jpeg_data  JPEG 图像数据
 * @param jpeg_len   JPEG 数据长度
 * @param question   OCR 识别出的题目文本
 * @param answer     完整讲解文本 (可为 NULL)
 * @return ESP_OK 成功
 */
esp_err_t photo_history_save(const uint8_t *jpeg_data, size_t jpeg_len,
                              const char *question, const char *answer);

/**
 * @brief 列出已保存的照片, 最新在前
 *
 * @param[out] entries    输出数组 (调用方分配)
 * @param[in]  max_count  数组容量
 * @param[out] out_count  实际条目数
 * @return ESP_OK 成功
 */
esp_err_t photo_history_list(photo_entry_t *entries, int max_count,
                              int *out_count);

/**
 * @brief 读取指定条目的 JPEG 和题目文本
 *
 * @param[in]  filename     文件名 (不含扩展名, 如 "20260704_153022")
 * @param[out] jpeg_data    输出: JPEG 数据 (需调用方 free())
 * @param[out] jpeg_len     输出: JPEG 数据长度
 * @param[out] question_buf 输出: 题目文本缓冲区
 * @param[in]  q_buf_size   question_buf 大小
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 文件不存在
 */
esp_err_t photo_history_get_entry(const char *filename,
                                   uint8_t **jpeg_data, size_t *jpeg_len,
                                   char *question_buf, size_t q_buf_size);

/**
 * @brief 读取指定条目的完整文本 (题目+答案)
 *
 * 将 .txt 文件内容全部读入缓冲区。调用方需确保缓冲区足够大。
 *
 * @param filename     文件名 (不含扩展名)
 * @param[out] buf     输出缓冲区
 * @param[in]  buf_size 缓冲区大小
 * @return ESP_OK 成功; ESP_ERR_NOT_FOUND 文件不存在
 */
esp_err_t photo_history_get_full_text(const char *filename,
                                       char *buf, size_t buf_size);

/**
 * @brief 删除一张照片及其配对的文本文件
 *
 * @param filename 文件名 (不含扩展名)
 * @return ESP_OK 成功
 */
esp_err_t photo_history_delete(const char *filename);

#ifdef __cplusplus
}
#endif
