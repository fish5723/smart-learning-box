/**
 * @file virtual_gamepad.c
 * @brief NES 触摸虚拟手柄实现
 *
 * 布局 (1024×600 屏幕):
 *   ┌────────────────────────────────────────┐
 *   │  NES 画面 (512×480, 居中)              │
 *   │                                        │
 *   │  ┌──────────┐        ┌──────┬──────┐  │
 *   │  │  ↑        │        │  Ⓑ  │  Ⓐ  │  │
 *   │  │ ← ○ →    │        ├──────┼──────┤  │
 *   │  │  ↓        │        │SELECT│START │  │
 *   │  └──────────┘        └──────┴──────┘  │
 *   └────────────────────────────────────────┘
 *
 * 方向键 8 方向触摸, AB 键独立按压。
 * GT911 支持 5 点触控, 可同时按方向键 + A/B。
 */

#include "virtual_gamepad.h"
#include "app/font_loader/font_loader.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "GAMEPAD";

/* ═══════════════════════════════════════════════
   颜色定义
   ═══════════════════════════════════════════════ */
#define GP_BG_COLOR       lv_color_hex(0x1E293B)
#define GP_BG_OPA         LV_OPA_60
#define GP_BTN_COLOR      lv_color_hex(0x475569)
#define GP_BTN_PRESSED    lv_color_hex(0x3B82F6)
#define GP_TEXT_COLOR     lv_color_hex(0xFFFFFF)
#define GP_AB_BG          lv_color_hex(0xDC2626)
#define GP_AB_PRESSED     lv_color_hex(0xEF4444)

/* ═══════════════════════════════════════════════
   布局常量
   ═══════════════════════════════════════════════ */
#define SCR_W             1024
#define SCR_H             600
#define DPAD_LEFT         40
#define DPAD_BOTTOM       (SCR_H - 160)
#define DPAD_SIZE         140
#define BTN_RADIUS        35
#define AB_RIGHT          (SCR_W - 150)
#define AB_BOTTOM         (SCR_H - 160)
#define FUNC_RIGHT        (SCR_W - 160)
#define FUNC_BOTTOM       (SCR_H - 40)

/* ═══════════════════════════════════════════════
   内部状态
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_dpad_container = NULL;
static lv_obj_t *s_ab_container   = NULL;
static bool      s_visible = true;

/* ═══════════════════════════════════════════════
   按键事件处理
   ═══════════════════════════════════════════════ */

static void on_dpad_press(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint8_t key = (uint8_t)(intptr_t)lv_obj_get_user_data(btn);
    nes_emu_input(key, true);

    /* 按下反馈: 变色 */
    lv_obj_set_style_bg_color(btn, GP_BTN_PRESSED, 0);
}

static void on_dpad_release(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    uint8_t key = (uint8_t)(intptr_t)lv_obj_get_user_data(btn);
    nes_emu_input(key, false);

    /* 释放反馈: 恢复原色 */
    lv_obj_set_style_bg_color(btn, GP_BTN_COLOR, 0);
}

static lv_obj_t *create_dpad_button(lv_obj_t *parent, int x, int y,
                                     int w, int h, uint8_t key, const char *label)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, GP_BTN_COLOR, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(btn, (void *)(intptr_t)key);
    lv_obj_add_event_cb(btn, on_dpad_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(btn, on_dpad_release, LV_EVENT_RELEASED, NULL);

    lv_obj_t *txt = lv_label_create(btn);
    lv_label_set_text(txt, label);
    lv_obj_set_style_text_color(txt, GP_TEXT_COLOR, 0);
    lv_obj_set_style_text_font(txt, g_font_cjk_16, 0);
    lv_obj_center(txt);

    return btn;
}

/* ═══════════════════════════════════════════════
   公开接口
   ═══════════════════════════════════════════════ */

void virtual_gamepad_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating virtual gamepad");

    /* ── 方向键区域 (左下) ── */
    s_dpad_container = lv_obj_create(parent);
    lv_obj_set_size(s_dpad_container, DPAD_SIZE, DPAD_SIZE);
    lv_obj_set_pos(s_dpad_container, DPAD_LEFT, DPAD_BOTTOM);
    lv_obj_set_style_bg_opa(s_dpad_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_dpad_container, 0, 0);
    lv_obj_set_style_pad_all(s_dpad_container, 0, 0);
    lv_obj_clear_flag(s_dpad_container, LV_OBJ_FLAG_SCROLLABLE);

    /* 方向键布局: 十字形排列
          ↑ (中心x, 顶部y)
       ←  → (中部)
          ↓ (中心x, 底部y) */
    int cx = DPAD_SIZE / 2;
    int cy = DPAD_SIZE / 2;
    int bw = 44;
    int bh = 44;

    /* ↑ */
    create_dpad_button(s_dpad_container, cx - bw/2, 0,
                       bw, bh, NES_KEY_UP, "↑");

    /* ← */
    create_dpad_button(s_dpad_container, 0, cy - bh/2,
                       bw, bh, NES_KEY_LEFT, "←");

    /* 中心圆 */
    create_dpad_button(s_dpad_container, cx - bw/2, cy - bh/2,
                       bw, bh, 0, "●");

    /* → */
    create_dpad_button(s_dpad_container, DPAD_SIZE - bw, cy - bh/2,
                       bw, bh, NES_KEY_RIGHT, "→");

    /* ↓ */
    create_dpad_button(s_dpad_container, cx - bw/2, DPAD_SIZE - bh,
                       bw, bh, NES_KEY_DOWN, "↓");

    /* ── AB 按钮区域 (右下) ── */
    s_ab_container = lv_obj_create(parent);
    lv_obj_set_size(s_ab_container, 160, 140);
    lv_obj_set_pos(s_ab_container, AB_RIGHT, AB_BOTTOM);
    lv_obj_set_style_bg_opa(s_ab_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ab_container, 0, 0);
    lv_obj_set_style_pad_all(s_ab_container, 0, 0);
    lv_obj_clear_flag(s_ab_container, LV_OBJ_FLAG_SCROLLABLE);

    /* Ⓑ 按钮 (左) */
    create_dpad_button(s_ab_container, 10, 10, 60, 60, NES_KEY_B, "Ⓑ");
    lv_obj_set_style_radius(lv_obj_get_child(s_ab_container, 0), LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(lv_obj_get_child(s_ab_container, 0), GP_AB_BG, 0);

    /* Ⓐ 按钮 (右) */
    create_dpad_button(s_ab_container, 90, 10, 60, 60, NES_KEY_A, "Ⓐ");
    lv_obj_set_style_radius(lv_obj_get_child(s_ab_container, 1), LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(lv_obj_get_child(s_ab_container, 1), GP_AB_BG, 0);

    /* SELECT */
    create_dpad_button(s_ab_container, 10, 85, 60, 30, NES_KEY_SELECT, "SEL");

    /* START */
    create_dpad_button(s_ab_container, 90, 85, 60, 30, NES_KEY_START, "STA");

    ESP_LOGI(TAG, "Virtual gamepad created");
}

void virtual_gamepad_set_visible(bool visible)
{
    s_visible = visible;

    if (s_dpad_container) {
        if (visible)
            lv_obj_clear_flag(s_dpad_container, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_dpad_container, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_ab_container) {
        if (visible)
            lv_obj_clear_flag(s_ab_container, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_ab_container, LV_OBJ_FLAG_HIDDEN);
    }
}
