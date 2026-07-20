/**
 * @file web_server.h
 * @brief 家长端网页 HTTP Server
 *
 * 提供 RESTful API 供家长浏览器访问学习进度数据
 * 
 * 访问方式: http://<设备IP>/dashboard
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动家长端网页服务
 * @return ESP_OK 成功
 */
esp_err_t web_server_start(void);

/**
 * @brief 停止网页服务
 * @return ESP_OK 成功
 */
esp_err_t web_server_stop(void);

/**
 * @brief 获取当前网页服务状态
 * @return true 运行中
 */
bool web_server_is_running(void);

#ifdef __cplusplus
}
#endif