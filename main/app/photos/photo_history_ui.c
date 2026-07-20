/**
 * @file photo_history_ui.c
 * @brief 拍照历史 UI 实现 — LVGL 9.x
 *
 * 页面布局 (1024x600):
 *   [Header: 返回 | 拍照历史 | 共N张]
 *   [Scrollable list of photo cards]
 *     [Timestamp] [JPEG size]
 *     [Question preview (truncated)]
 *     [查看详情 btn] [删除 btn]
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md:
 *   - Dark Theme (Background=#0F172A, Card=#1E293B)
 *   - Colors: Primary=#3B82F6, Success=#10B981, Danger=#EF4444
 *   - Radius: Card=16, Button=12
 */

#include "photo_history_ui.h"
#include "photo_history.h"
#include "home_ui.h"
#include "home.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char *TAG = "PHOTO_UI";

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
#define ENTRY_CARD_H     120
#define BTN_H            36

/* ═══════════════════════════════════════════════
   Global objects
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_count_label  = NULL;
static lv_obj_t *s_list         = NULL;
static lv_obj_t *s_empty_state  = NULL;
static lv_obj_t *s_toast        = NULL;

/* ── 缓存最新加载的条目 (PSRAM 动态分配,避免 ~19KB BSS) ── */
#define MAX_PHOTOS_CACHE 100
static photo_entry_t *s_entries = NULL;
static int            s_entry_count = 0;

/* ═══════════════════════════════════════════════
   Detail view objects
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_detail_panel  = NULL;
static lv_obj_t *s_detail_title  = NULL;
static lv_obj_t *s_detail_text   = NULL;
static char      s_detail_filename[PHOTO_HISTORY_MAX_FILENAME];

/* ═══════════════════════════════════════════════
   Internal function declarations
   ═══════════════════════════════════════════════ */
static void create_header(lv_obj_t *parent);
static void create_list_area(lv_obj_t *parent);
static void create_empty_state(lv_obj_t *parent);
static void create_toast(lv_obj_t *parent);
static void refresh_list(void);
static void show_toast(const char *text);
static void show_detail(const char *filename);
static void hide_detail(void);
static void on_back_click(lv_event_t *e);
static void on_detail_click(lv_event_t *e);
static void on_delete_click(lv_event_t *e);
static void on_detail_back_click(lv_event_t *e);
static void anim_toast_opa_cb(lv_obj_t *obj, int32_t v);
static void on_toast_fade_completed(lv_anim_t *anim);

/* ═══════════════════════════════════════════════
   Page lifecycle
   ═══════════════════════════════════════════════ */

static void photo_history_ui_create_screen(void)
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

void photo_history_ui_init(void)
{
    ESP_LOGI(TAG, "photo_history_ui_init() — deferred");

    /* PSRAM 分配条目缓存 (避免 ~19KB BSS) */
    if (!s_entries) {
        s_entries = heap_caps_malloc(MAX_PHOTOS_CACHE * sizeof(photo_entry_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_entries) {
            ESP_LOGE(TAG, "PSRAM alloc for entries failed — list disabled");
        }
    }
}

void photo_history_ui_show(void)
{
    if (!s_screen) {
        photo_history_ui_create_screen();
    }
    if (s_screen) {
        refresh_list();
        lv_screen_load(s_screen);
    }
}

void photo_history_ui_hide(void)
{
    /* return to home */
}

/* ═══════════════════════════════════════════════
   Header (Height=60, Radius=16)
   [返回]  [拍照历史]  [共N张]
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
    lv_label_set_text(title, "拍照历史");
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* Count */
    s_count_label = lv_label_create(header);
    lv_label_set_text(s_count_label, "共0张");
    lv_obj_set_style_text_color(s_count_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_count_label, g_font_cjk_16, 0);
}

/* ═══════════════════════════════════════════════
   List area (flex_grow=1, scrollable)
   ═══════════════════════════════════════════════ */

static void create_list_area(lv_obj_t *parent)
{
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

    lv_obj_t *icon = icon_loader_create_image(s_empty_state, ICON_FLASH_CAMERA, 72, 72);
    LV_UNUSED(icon);

    lv_obj_t *empty_text = lv_label_create(s_empty_state);
    lv_label_set_text(empty_text, "还没有拍照记录");
    lv_obj_set_style_text_color(empty_text, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(empty_text, g_font_cjk_20, 0);

    lv_obj_t *hint = lv_label_create(s_empty_state);
    lv_label_set_text(hint, "拍照解题后自动保存");
    lv_obj_set_style_text_color(hint, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   Single photo card
   ═══════════════════════════════════════════════ */

static lv_obj_t *create_photo_card(lv_obj_t *parent, photo_entry_t *entry)
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

    /* ── Row 1: Timestamp + JPEG size ── */
    lv_obj_t *top_row = lv_obj_create(card);
    lv_obj_set_size(top_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 0, 0);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ts_label = lv_label_create(top_row);
    lv_label_set_text(ts_label, entry->timestamp);
    lv_obj_set_style_text_color(ts_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ts_label, g_font_cjk_16, 0);

    char size_buf[32];
    if (entry->jpeg_size >= 1024) {
        lv_snprintf(size_buf, sizeof(size_buf), "%u KB",
                    (unsigned)(entry->jpeg_size / 1024));
    } else {
        lv_snprintf(size_buf, sizeof(size_buf), "%u B", (unsigned)entry->jpeg_size);
    }
    lv_obj_t *size_label = lv_label_create(top_row);
    lv_label_set_text(size_label, size_buf);
    lv_obj_set_style_text_color(size_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(size_label, g_font_cjk_14, 0);

    /* ── Row 2: Question preview ── */
    lv_obj_t *q_label = lv_label_create(card);
    lv_label_set_text(q_label, entry->question[0] ? entry->question : "(无题目)");
    lv_obj_set_style_text_color(q_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(q_label, g_font_cjk_14, 0);
    lv_label_set_long_mode(q_label, LV_LABEL_LONG_WRAP);

    /* ── Row 3: Action buttons ── */
    lv_obj_t *btn_row = lv_obj_create(card);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_gap(btn_row, SPACING_SM, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
        LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    /* "查看详情" button */
    lv_obj_t *detail_btn = lv_obj_create(btn_row);
    lv_obj_set_size(detail_btn, 100, BTN_H);
    lv_obj_set_style_bg_color(detail_btn, COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(detail_btn, 10, 0);
    lv_obj_set_style_border_width(detail_btn, 0, 0);
    lv_obj_add_flag(detail_btn, LV_OBJ_FLAG_CLICKABLE);
    /* Store filename pointer in user data */
    lv_obj_add_event_cb(detail_btn, on_detail_click, LV_EVENT_CLICKED,
                        (void *)entry->filename);
    lv_obj_clear_flag(detail_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *detail_text = lv_label_create(detail_btn);
    lv_label_set_text(detail_text, "查看详情");
    lv_obj_set_style_text_color(detail_text, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(detail_text, g_font_cjk_14, 0);
    lv_obj_center(detail_text);

    /* "删除" button */
    lv_obj_t *del_btn = lv_obj_create(btn_row);
    lv_obj_set_size(del_btn, 60, BTN_H);
    lv_obj_set_style_bg_color(del_btn, COLOR_DANGER, 0);
    lv_obj_set_style_radius(del_btn, 10, 0);
    lv_obj_set_style_border_width(del_btn, 0, 0);
    lv_obj_add_flag(del_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(del_btn, on_delete_click, LV_EVENT_CLICKED,
                        (void *)entry->filename);
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

    if (!s_entries) {
        ESP_LOGW(TAG, "Entries not allocated (no PSRAM?)");
        return;
    }

    /* Remove existing cards (skip empty state) */
    uint32_t child_cnt = lv_obj_get_child_cnt(s_list);
    for (uint32_t i = child_cnt; i > 0; i--) {
        lv_obj_t *child = lv_obj_get_child(s_list, i - 1);
        if (child && child != s_empty_state) {
            lv_obj_delete(child);
        }
    }

    s_entry_count = 0;
    esp_err_t ret = photo_history_list(s_entries, MAX_PHOTOS_CACHE, &s_entry_count);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to load photos: %s", esp_err_to_name(ret));
    }

    if (s_count_label) {
        char buf[32];
        lv_snprintf(buf, sizeof(buf), "共%d张", s_entry_count);
        lv_label_set_text(s_count_label, buf);
    }

    if (s_entry_count == 0) {
        lv_obj_clear_flag(s_empty_state, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_empty_state, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < s_entry_count; i++) {
            create_photo_card(s_list, &s_entries[i]);
        }
    }

    ESP_LOGI(TAG, "List refreshed: %d photos", s_entry_count);
}

/* ═══════════════════════════════════════════════
   Detail view (overlay panel)
   ═══════════════════════════════════════════════ */

static void show_detail(const char *filename)
{
    if (!filename || !s_screen) return;

    /* Get JPEG size for display */
    size_t   jpeg_len  = 0;

    /* Get JPEG size via stat */
    char jpg_path[192];
    snprintf(jpg_path, sizeof(jpg_path), "/sdcard/photos/%s.jpg", filename);
    struct stat jst;
    if (stat(jpg_path, &jst) == 0) {
        jpeg_len = (size_t)jst.st_size;
    }

    /* Load full text from .txt file */
    char full_text[4096] = {0};
    esp_err_t ret = photo_history_get_full_text(filename,
                                                 full_text, sizeof(full_text));
    if (ret != ESP_OK) {
        /* Fallback: try get_entry for just the question */
        uint8_t *jd = NULL;
        size_t   jl = 0;
        ret = photo_history_get_entry(filename, &jd, &jl, full_text, sizeof(full_text));
        if (jd) free(jd);
        if (ret != ESP_OK) {
            show_toast("无法加载详情");
            return;
        }
    }

    /* Save filename for delete from detail view */
    strncpy(s_detail_filename, filename, sizeof(s_detail_filename) - 1);
    s_detail_filename[sizeof(s_detail_filename) - 1] = '\0';

    /* Create overlay panel */
    s_detail_panel = lv_obj_create(s_screen);
    lv_obj_set_size(s_detail_panel, SCREEN_W - 60, SCREEN_H - 80);
    lv_obj_set_style_bg_color(s_detail_panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_detail_panel, 20, 0);
    lv_obj_set_style_border_width(s_detail_panel, 2, 0);
    lv_obj_set_style_border_color(s_detail_panel, COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(s_detail_panel, SPACING_LG, 0);
    lv_obj_set_style_pad_gap(s_detail_panel, SPACING_MD, 0);
    lv_obj_set_flex_flow(s_detail_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(s_detail_panel);
    /* Raise above other content */
    lv_obj_add_flag(s_detail_panel, LV_OBJ_FLAG_FLOATING);

    /* Title row: [Back] [timestamp] [Delete] */
    lv_obj_t *title_row = lv_obj_create(s_detail_panel);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn = lv_obj_create(title_row);
    lv_obj_set_size(back_btn, 80, BTN_H);
    lv_obj_set_style_bg_color(back_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_btn, on_detail_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "关闭");
    lv_obj_set_style_text_color(back_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_label, g_font_cjk_14, 0);
    lv_obj_center(back_label);

    /* Timestamp + JPEG size */
    s_detail_title = lv_label_create(title_row);
    char ts_buf[64];
    lv_snprintf(ts_buf, sizeof(ts_buf), "%s  (%u KB)",
                filename,
                (unsigned)(jpeg_len / 1024));
    lv_label_set_text(s_detail_title, ts_buf);
    lv_obj_set_style_text_color(s_detail_title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(s_detail_title, g_font_cjk_16, 0);

    /* Delete button (in detail) */
    lv_obj_t *del_btn = lv_obj_create(title_row);
    lv_obj_set_size(del_btn, 60, BTN_H);
    lv_obj_set_style_bg_color(del_btn, COLOR_DANGER, 0);
    lv_obj_set_style_radius(del_btn, 10, 0);
    lv_obj_set_style_border_width(del_btn, 0, 0);
    lv_obj_add_flag(del_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(del_btn, on_delete_click, LV_EVENT_CLICKED,
                        (void *)filename);
    lv_obj_clear_flag(del_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *del_text = lv_label_create(del_btn);
    lv_label_set_text(del_text, "删除");
    lv_obj_set_style_text_color(del_text, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(del_text, g_font_cjk_14, 0);
    lv_obj_center(del_text);

    /* Question/Answer text (scrollable) */
    lv_obj_t *text_area = lv_obj_create(s_detail_panel);
    lv_obj_set_size(text_area, LV_PCT(100), 0);
    lv_obj_set_flex_grow(text_area, 1);
    lv_obj_set_style_bg_color(text_area, COLOR_BG, 0);
    lv_obj_set_style_radius(text_area, 12, 0);
    lv_obj_set_style_pad_all(text_area, SPACING_MD, 0);
    lv_obj_set_style_border_width(text_area, 0, 0);
    lv_obj_add_flag(text_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(text_area, LV_DIR_VER);

    s_detail_text = lv_label_create(text_area);
    lv_label_set_text(s_detail_text, full_text[0] ? full_text : "(无内容)");
    lv_obj_set_width(s_detail_text, LV_PCT(100));   /* 必须固定宽度, 否则 WRAP 不生效 */
    lv_obj_set_style_text_color(s_detail_text, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_detail_text, g_font_cjk_16, 0);
    lv_label_set_long_mode(s_detail_text, LV_LABEL_LONG_WRAP);
}

static void hide_detail(void)
{
    if (s_detail_panel) {
        lv_obj_delete(s_detail_panel);
        s_detail_panel = NULL;
        s_detail_title = NULL;
        s_detail_text  = NULL;
    }
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
    hide_detail();
    home_show();
}

static void on_detail_click(lv_event_t *e)
{
    const char *filename = (const char *)lv_event_get_user_data(e);
    if (!filename) return;
    ESP_LOGI(TAG, "Show detail: %s", filename);
    show_detail(filename);
}

static void on_delete_click(lv_event_t *e)
{
    const char *filename = (const char *)lv_event_get_user_data(e);
    if (!filename) return;
    ESP_LOGI(TAG, "Delete photo: %s", filename);

    esp_err_t ret = photo_history_delete(filename);
    if (ret == ESP_OK) {
        show_toast("已删除");
        hide_detail();
        refresh_list();
    } else {
        show_toast("删除失败");
    }
}

static void on_detail_back_click(lv_event_t *e)
{
    (void)e;
    hide_detail();
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
