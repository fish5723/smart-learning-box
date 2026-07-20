/**
 * @file wifi_ui.h
 * @brief WiFi 设置页面 — LVGL 9.x UI
 *
 * 页面元素:
 *   - 状态卡片 (当前网络 / SSID / IP / RSSI / 连接状态)
 *   - WiFi 列表 (可滚动, 信号强度, 加密类型, 连接按钮)
 *   - 重新扫描按钮
 *
 * 通过 wifi_register_callback() 接收 BSP 层状态通知。
 */

#pragma once

#include "lvgl.h"
#include "wifi.h"          /* wifi_scan_item_t, 全局变量声明, 回调类型 */

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════
   页面生命周期
   ═══════════════════════════════════════════════ */

void wifi_ui_init(void);
void wifi_ui_show(void);
void wifi_ui_hide(void);

/* ═══════════════════════════════════════════════
   数据更新 (由 WiFi 事件回调通过 lv_async_call 调用)
   ═══════════════════════════════════════════════ */

void wifi_ui_update_scan_list(void);
void wifi_ui_update_status(void);

#ifdef __cplusplus
}
#endif
