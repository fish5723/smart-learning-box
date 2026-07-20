/**
 * @file font_loader.h
 * @brief TF 卡字库加载器 — 从 SD 卡加载 LVGL BinFont
 *
 * 功能:
 *   - 从 /sdcard/fonts/ 目录加载 *.bin 中文字体 (10 个字号: 12~48)
 *   - SD 卡不可用时回退到固件内置最小字体
 *   - 释放 ~15MB Flash 空间
 *
 * 字号使用建议:
 *   12px — 辅助文字、水印、超小标签
 *   14px — 描述文字、状态栏信息、列表副标题
 *   16px — 正文、按钮、列表项
 *   18px — 增强正文、小标题
 *   20px — 卡片标题、统计数值
 *   24px — 页面标题、大按钮
 *   26px — 欢迎语、弹窗标题
 *   32px — Banner 大字、强调标题
 *   36px — 超大标题、Logo
 *   48px — 特大号数字、首页核心文字
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 全局字体指针（供所有 UI 模块使用）
 */
extern lv_font_t *g_font_cjk_12;
extern lv_font_t *g_font_cjk_14;
extern lv_font_t *g_font_cjk_16;
extern lv_font_t *g_font_cjk_18;
extern lv_font_t *g_font_cjk_20;
extern lv_font_t *g_font_cjk_24;
extern lv_font_t *g_font_cjk_26;
extern lv_font_t *g_font_cjk_32;
extern lv_font_t *g_font_cjk_36;
extern lv_font_t *g_font_cjk_48;

/**
 * @brief 初始化字体加载器
 *
 * 优先从 TF 卡加载字体，失败时使用固件内置 fallback。
 * 必须在 sd_card_init() 和 lvgl_port_init() 之后调用。
 *
 * @return ESP_OK 成功（至少加载了一个字体）
 */
esp_err_t font_loader_init(void);

/**
 * @brief 反初始化字体加载器
 *
 * 释放从 TF 卡加载的字体内存。
 */
void font_loader_deinit(void);

/**
 * @brief 检查是否使用了 TF 卡字体
 * @return true 从 TF 卡加载成功
 */
bool font_loader_using_sd(void);

#ifdef __cplusplus
}
#endif