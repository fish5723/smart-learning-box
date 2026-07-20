/**
 * @file wrong_book_ui.c
 * @brief 错题本 UI 实现 — LVGL 9.x
 *
 * 页面布局 (1024x600):
 *   [Header: 返回 | 错题本 | 共N题]
 *   [Scrollable list of entry cards]
 *     [Question preview (truncated)]
 *     [Subject badge] [Tags]
 *     [Timestamp] [Review: N次]
 *     [已复习 btn] [删除 btn]
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md:
 *   - Dark Theme (Background=#0F172A, Card=#1E293B)
 *   - Colors: Primary=#3B82F6, Success=#10B981, Danger=#EF4444
 *   - Radius: Card=16, Button=12
 */

#include "wrong_book_ui.h"
#include "wrong_book.h"
#include "home_ui.h"
#include "home.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WRONG_BOOK_UI";

/* ═══════════════════════════════════════════════
   Colors (UI_DESIGN_SYSTEM.md S2)
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_DANGER          lv_color_hex(0xEF4444)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TITLE_BLUE      lv_color_hex(0x60A5FA)
#define COLOR_WARNING_BG      lv_color_hex(0x7C3AED)

/* ═══════════════════════════════════════════════
   Spacing (UI_DESIGN_SYSTEM.md S5)
   ═══════════════════════════════════════════════ */
#define SPACING_SM  8
#define SPACING_MD  12
#define SPACING_LG  16

/* ═══════════════════════════════════════════════
   Layout constants (1024x600)
   ═══════════════════════════════════════════════ */
#define SCREEN_W         1024
#define SCREEN_H         600
#define CONTAINER_PAD    16
#define ITEM_GAP         12
#define HEADER_H         60
#define ENTRY_CARD_H     140
#define BTN_H            36

/* ═══════════════════════════════════════════════
   Global objects
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_count_label  = NULL;
static lv_obj_t *s_list         = NULL;
static lv_obj_t *s_empty_state  = NULL;
static lv_obj_t *s_toast        = NULL;

/* ── 缓存最新加载的条目数据 ── */
#define MAX_ENTRIES_CACHE 100
static wrong_entry_t s_entries[MAX_ENTRIES_CACHE];
static int            s_entry_count = 0;

/* ═══════════════════════════════════════════════
   Internal function declarations
   ═══════════════════════════════════════════════ */
static void create_header(lv_obj_t *parent);
static void create_list_area(lv_obj_t *parent);
static void create_empty_state(lv_obj_t *parent);
static void create_toast(lv_obj_t *parent);
static void refresh_list(void);
static void show_toast(const char *text);
static void on_back_click(lv_event_t *e);
static void on_review_click(lv_event_t *e);
static void on_delete_click(lv_event_t *e);
static void anim_toast_opa_cb(lv_obj_t *obj, int32_t v);
static void on_toast_fade_completed(lv_anim_t *anim);

/* ═══════════════════════════════════════════════
   Page lifecycle
   ═══════════════════════════════════════════════ */

static void wrong_book_ui_create_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);
    create_list_area(s_screen);
    create_toast(s_screen);
}

void wrong_book_ui_init(void)
{
    ESP_LOGI(TAG, "wrong_book_ui_init() — deferred");
}

void wrong_book_ui_show(void)
{
    if (!s_screen) {
        wrong_book_ui_create_screen();
    }
    if (s_screen) {
        refresh_list();
        lv_screen_load(s_screen);
    }
}

void wrong_book_ui_hide(void)
{
    /* return to home */
}

/* ═══════════════════════════════════════════════
   Header (Height=60, Radius=16)
   [返回]  [错题本]  [共N题]
   ═══════════════════════════════════════════════ */

static void create_header(lv_obj_t *parent)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_color(header, COLOR_CARD, 0);
    lv_obj_set_style_radius(header, 16, 0);
    lv_obj_set_style_pad_hor(header, 20, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* Back button */
    lv_obj_t *back_btn = lv_obj_create(header);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_set_style_bg_color(back_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_icon = lv_label_create(back_btn);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_icon, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_14, 0);
    lv_obj_align(back_icon, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_color(back_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_label, g_font_cjk_14, 0);
    lv_obj_align(back_label, LV_ALIGN_CENTER, 5, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "错题本");
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* Entry count */
    s_count_label = lv_label_create(header);
    lv_label_set_text(s_count_label, "共0题");
    lv_obj_set_style_text_color(s_count_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_count_label, g_font_cjk_16, 0);
}

/* ═══════════════════════════════════════════════
   List area (flex_grow=1, scrollable)
   ═══════════════════════════════════════════════ */

static void create_list_area(lv_obj_t *parent)
{
    /* Scrollable container */
    s_list = lv_obj_create(parent);
    lv_obj_set_size(s_list, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_gap(s_list, SPACING_MD, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    /* Empty state (hidden when entries exist) */
    create_empty_state(s_list);
}

static void create_empty_state(lv_obj_t *parent)
{
    s_empty_state = lv_obj_create(parent);
    lv_obj_set_size(s_empty_state, LV_PCT(100), 200);
    lv_obj_set_style_bg_color(s_empty_state, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_empty_state, 16, 0);
    lv_obj_set_style_border_width(s_empty_state, 0, 0);
    lv_obj_set_flex_flow(s_empty_state, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_empty_state, SPACING_MD, 0);
    lv_obj_set_flex_align(s_empty_state,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_empty_state, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = icon_loader_create_image(s_empty_state, ICON_BOOKS, 72, 72);
    LV_UNUSED(icon);

    lv_obj_t *empty_text = lv_label_create(s_empty_state);
    lv_label_set_text(empty_text, "还没有错题记录");
    lv_obj_set_style_text_color(empty_text, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(empty_text, g_font_cjk_20, 0);

    lv_obj_t *hint = lv_label_create(s_empty_state);
    lv_label_set_text(hint, "拍照解题后可标记错题");
    lv_obj_set_style_text_color(hint, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   Create single entry card
   ═══════════════════════════════════════════════ */

static lv_obj_t *create_entry_card(lv_obj_t *parent, wrong_entry_t *entry)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), ENTRY_CARD_H);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, SPACING_MD, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(card, SPACING_SM, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Row 1: Question preview (truncated to ~60 chars) ── */
    char q_buf[128];
    size_t q_len = strlen(entry->question);
    if (q_len > 60) {
        memcpy(q_buf, entry->question, 60);
        q_buf[60] = '.';
        q_buf[61] = '.';
        q_buf[62] = '.';
        q_buf[63] = '\0';
    } else {
        strcpy(q_buf, entry->question);
    }

    lv_obj_t *q_label = lv_label_create(card);
    lv_label_set_text(q_label, q_buf);
    lv_obj_set_style_text_color(q_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(q_label, g_font_cjk_16, 0);
    lv_label_set_long_mode(q_label, LV_LABEL_LONG_WRAP);

    /* ── Row 2: Subject badge + Tags ── */
    lv_obj_t *meta_row = lv_obj_create(card);
    lv_obj_set_size(meta_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(meta_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(meta_row, 0, 0);
    lv_obj_set_style_pad_all(meta_row, 0, 0);
    lv_obj_set_style_pad_gap(meta_row, SPACING_SM, 0);
    lv_obj_set_flex_flow(meta_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(meta_row, LV_OBJ_FLAG_SCROLLABLE);

    if (entry->subject[0]) {
        lv_obj_t *subj_badge = lv_obj_create(meta_row);
        lv_obj_set_size(subj_badge, LV_SIZE_CONTENT, 24);
        lv_obj_set_style_bg_color(subj_badge, COLOR_PRIMARY, 0);
        lv_obj_set_style_radius(subj_badge, 12, 0);
        lv_obj_set_style_pad_hor(subj_badge, SPACING_SM, 0);
        lv_obj_set_style_pad_ver(subj_badge, 2, 0);
        lv_obj_set_style_border_width(subj_badge, 0, 0);
        lv_obj_clear_flag(subj_badge, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *subj_text = lv_label_create(subj_badge);
        lv_label_set_text(subj_text, entry->subject);
        lv_obj_set_style_text_color(subj_text, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(subj_text, g_font_cjk_14, 0);
        lv_obj_center(subj_text);
    }

    if (entry->tags[0]) {
        lv_obj_t *tag_label = lv_label_create(meta_row);
        lv_label_set_text(tag_label, entry->tags);
        lv_obj_set_style_text_color(tag_label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(tag_label, g_font_cjk_14, 0);
    }

    /* ── Row 3: Timestamp + review count + action buttons ── */
    lv_obj_t *bottom_row = lv_obj_create(card);
    lv_obj_set_size(bottom_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(bottom_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bottom_row, 0, 0);
    lv_obj_set_style_pad_all(bottom_row, 0, 0);
    lv_obj_set_style_pad_gap(bottom_row, SPACING_SM, 0);
    lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bottom_row,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bottom_row, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: timestamp + review count */
    lv_obj_t *info_area = lv_obj_create(bottom_row);
    lv_obj_set_size(info_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_area, 0, 0);
    lv_obj_set_style_pad_all(info_area, 0, 0);
    lv_obj_set_style_pad_gap(info_area, SPACING_MD, 0);
    lv_obj_set_flex_flow(info_area, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(info_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ts_label = lv_label_create(info_area);
    lv_label_set_text(ts_label, entry->timestamp);
    lv_obj_set_style_text_color(ts_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ts_label, g_font_cjk_14, 0);

    char review_buf[32];
    lv_snprintf(review_buf, sizeof(review_buf), "复习%d次", entry->review_count);
    lv_obj_t *review_label = lv_label_create(info_area);
    lv_label_set_text(review_label, review_buf);
    lv_obj_set_style_text_color(review_label,
        entry->review_count > 0 ? COLOR_SUCCESS : COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(review_label, g_font_cjk_14, 0);

    /* Right: action buttons */
    lv_obj_t *btn_area = lv_obj_create(bottom_row);
    lv_obj_set_size(btn_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_area, 0, 0);
    lv_obj_set_style_pad_all(btn_area, 0, 0);
    lv_obj_set_style_pad_gap(btn_area, SPACING_SM, 0);
    lv_obj_set_flex_flow(btn_area, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(btn_area, LV_OBJ_FLAG_SCROLLABLE);

    /* "已复习" button */
    lv_obj_t *review_btn = lv_obj_create(btn_area);
    lv_obj_set_size(review_btn, 80, BTN_H);
    lv_obj_set_style_bg_color(review_btn, COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(review_btn, 10, 0);
    lv_obj_set_style_border_width(review_btn, 0, 0);
    lv_obj_add_flag(review_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(review_btn, on_review_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)entry->id);
    lv_obj_clear_flag(review_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *review_text = lv_label_create(review_btn);
    lv_label_set_text(review_text, "已复习");
    lv_obj_set_style_text_color(review_text, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(review_text, g_font_cjk_14, 0);
    lv_obj_center(review_text);

    /* "删除" button */
    lv_obj_t *del_btn = lv_obj_create(btn_area);
    lv_obj_set_size(del_btn, 60, BTN_H);
    lv_obj_set_style_bg_color(del_btn, COLOR_DANGER, 0);
    lv_obj_set_style_radius(del_btn, 10, 0);
    lv_obj_set_style_border_width(del_btn, 0, 0);
    lv_obj_add_flag(del_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(del_btn, on_delete_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)entry->id);
    lv_obj_clear_flag(del_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *del_text = lv_label_create(del_btn);
    lv_label_set_text(del_text, "删除");
    lv_obj_set_style_text_color(del_text, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(del_text, g_font_cjk_14, 0);
    lv_obj_center(del_text);

    return card;
}

/* ═══════════════════════════════════════════════
   Refresh list from SD card
   ═══════════════════════════════════════════════ */

static void refresh_list(void)
{
    if (!s_list) return;

    /* Remove all existing entry cards (skip empty state and scrollbar) */
    uint32_t child_cnt = lv_obj_get_child_cnt(s_list);
    for (uint32_t i = child_cnt; i > 0; i--) {
        lv_obj_t *child = lv_obj_get_child(s_list, i - 1);
        if (child && child != s_empty_state) {
            lv_obj_delete(child);
        }
    }

    /* Load entries */
    s_entry_count = 0;
    esp_err_t ret = wrong_book_list(s_entries, MAX_ENTRIES_CACHE, &s_entry_count);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to load entries: %s", esp_err_to_name(ret));
    }

    /* Update count label */
    if (s_count_label) {
        char buf[32];
        lv_snprintf(buf, sizeof(buf), "共%d题", s_entry_count);
        lv_label_set_text(s_count_label, buf);
    }

    if (s_entry_count == 0) {
        lv_obj_clear_flag(s_empty_state, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_empty_state, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < s_entry_count; i++) {
            create_entry_card(s_list, &s_entries[i]);
        }
    }

    ESP_LOGI(TAG, "List refreshed: %d entries", s_entry_count);
}

/* ═══════════════════════════════════════════════
   Toast
   ═══════════════════════════════════════════════ */

static void create_toast(lv_obj_t *parent)
{
    s_toast = lv_obj_create(parent);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_toast, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_toast, 16, 0);
    lv_obj_set_style_border_width(s_toast, 1, 0);
    lv_obj_set_style_border_color(s_toast, COLOR_BORDER, 0);
    lv_obj_set_style_pad_hor(s_toast, 40, 0);
    lv_obj_set_style_pad_ver(s_toast, 20, 0);
    lv_obj_set_style_shadow_width(s_toast, 40, 0);
    lv_obj_set_style_shadow_color(s_toast, lv_color_hex(0x000000), 0);
    lv_obj_set_style_opa(s_toast, LV_OPA_TRANSP, 0);
    lv_obj_center(s_toast);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(s_toast);
    lv_label_set_text(label, "");
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, g_font_cjk_20, 0);
    lv_obj_center(label);
}

static void show_toast(const char *text)
{
    if (!s_toast) return;

    lv_obj_t *label = lv_obj_get_child(s_toast, 0);
    if (label) lv_label_set_text(label, text);

    lv_obj_set_style_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_toast);
    lv_anim_set_delay(&a, 2000);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_toast_opa_cb);
    lv_anim_set_completed_cb(&a, on_toast_fade_completed);
    lv_anim_start(&a);
}

/* ═══════════════════════════════════════════════
   Event handlers
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Back to home");
    home_show();
}

static void on_review_click(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Mark reviewed: id=%d", id);

    esp_err_t ret = wrong_book_mark_reviewed(id);
    if (ret == ESP_OK) {
        show_toast("已标记为复习!");
        refresh_list();
    } else {
        show_toast("操作失败");
    }
}

static void on_delete_click(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Delete entry: id=%d", id);

    esp_err_t ret = wrong_book_delete(id);
    if (ret == ESP_OK) {
        show_toast("已删除");
        refresh_list();
    } else if (ret == ESP_ERR_NOT_FOUND) {
        show_toast("该条目不存在");
    } else {
        show_toast("删除失败");
    }
}

/* ═══════════════════════════════════════════════
   Animation callbacks
   ═══════════════════════════════════════════════ */

static void anim_toast_opa_cb(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, v, 0);
}

static void on_toast_fade_completed(lv_anim_t *anim)
{
    (void)anim;
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}
