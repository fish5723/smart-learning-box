/**
 * @file boot_ui.h
 * @brief Boot Splash 页面 — 启动动画 UI 层
 *
 * 设计语言: ChatGPT × Nintendo Switch (Dark Theme)
 * 严格继承 UI_DESIGN_SYSTEM.md:
 *   - Background #0F172A  /  Logo Blue #60A5FA  /  Primary #3B82F6
 *   - Success #10B981  /  Text Secondary #94A3B8
 *   - Radius 8px  /  动画: Fade ease_out 300-500ms
 *
 * 线程安全:
 *   所有 LVGL API 调用通过 lv_async_call() 转发到 LVGL task。
 *   调用方可从任意线程调用本模块接口，无需持锁。
 *
 * 职责边界:
 *   - 创建 Splash 页面 + 动画 (via lv_async_call)
 *   - 更新进度条 + 状态文字 (via lv_async_call)
 *   - BOOT_TIMEOUT 保护 (15s 超时强制退出)
 *   - 完成退出 (清屏 + 退出回调)
 *   不初始化 WiFi / Camera / NVS / 创建 FreeRTOS task。
 *   不依赖 home.h / wifi_ui.h (通过退出回调 + ui_manager 解耦)。
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*boot_ui_on_exit_cb_t)(void);

/**
 * @brief 异步启动 Boot Splash 页面
 *
 * 通过 lv_async_call() 将全部 LVGL 对象创建推迟到 LVGL task 上下文执行。
 * 必须在 LVGL 初始化完成且 task 稳定运行后调用 (建议 delay >= 200ms)。
 * 调用方无需持锁。
 */
void boot_ui_start(void);

/**
 * @brief 更新进度条 + 状态文字 (线程安全, lv_async_call)
 */
void boot_ui_set_progress(uint8_t percent, const char *status_text);

/**
 * @brief 完成启动流程 (显示 READY → 500ms → 清屏 → exit callback)
 *
 * @param on_exit 退出回调 (→ ui_manager_show_page), NULL 则跳过
 */
void boot_ui_finish(boot_ui_on_exit_cb_t on_exit);

#ifdef __cplusplus
}
#endif
