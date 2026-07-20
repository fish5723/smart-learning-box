/**
 * @file error_code.h
 * @brief Vision AI Platform — 统一错误码（模块前缀编号）
 *
 * 命名规则: ERR_<模块>_<原因>
 * Camera:   0x01xx
 * HTTP:     0x02xx
 * JSON:     0x03xx
 * Vision:   0x04xx
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ── Common: 成功返回 0, 不需要宏 ── */

/* ── Camera (0x01xx) ── */
#define ERR_CAMERA_NOT_READY         0x0101
#define ERR_CAMERA_CAPTURE_TIMEOUT   0x0102
#define ERR_CAMERA_JPEG_ENCODE       0x0103
#define ERR_CAMERA_BUSY              0x0104

/* ── HTTP (0x02xx) ── */
#define ERR_HTTP_CONNECT             0x0201
#define ERR_HTTP_TIMEOUT             0x0202
#define ERR_HTTP_STATUS              0x0203
#define ERR_HTTP_CANCELLED           0x0204

/* ── JSON (0x03xx) ── */
#define ERR_JSON_PARSE               0x0301
#define ERR_JSON_MISSING_FIELD       0x0302

/* ── Vision (0x04xx) ── */
#define ERR_VISION_BUSY              0x0401
#define ERR_VISION_CAPTURE_FAILED    0x0402
#define ERR_VISION_UPLOAD_FAILED     0x0403
#define ERR_VISION_PROCESS_FAILED    0x0404
#define ERR_VISION_CANCELLED         0x0405
#define ERR_VISION_TIMEOUT           0x0406

#ifdef __cplusplus
}
#endif