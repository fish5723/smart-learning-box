/**
 * @file nes_game_list_ui.c
 * @brief NES 游戏列表页面 — lv_list 分页浏览 (每页最多 30 条)
 *
 * 数据来自 game_db 的 PSRAM 常驻索引, 不扫描 ROM 目录。
 * 每页最多 30 个 lv_list button, 通过 lv_obj_clean() + 重建实现分页。
 * 点击游戏 → game_db_get_rom_path() → nes_emu_load_rom()。
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md:
 *   - Dark Theme, 1024×600
 *   - 字体安全: 仅使用 ≤cjk_24
 *   - 无图片, 无 lv_image
 */

#include "nes_game_list_ui.h"
#include "nes_category_ui.h"
#include "app/game_center/game_db/game_db.h"
#include "app/nes_emu/nes_emu.h"
#include "app/nes_emu/nes_emu_ui.h"
#include "app/font_loader/font_loader.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "NES_LIST_UI";

/* ── 外部字体 ── */
extern lv_font_t *g_font_cjk_12;
extern lv_font_t *g_font_cjk_14;
extern lv_font_t *g_font_cjk_16;
extern lv_font_t *g_font_cjk_18;
extern lv_font_t *g_font_cjk_20;
extern lv_font_t *g_font_cjk_24;

/* ═══════════════════════════════════════════════
   颜色 (与 game_center_ui.c 统一)
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TITLE_BLUE      lv_color_hex(0x60A5FA)

/* ═══════════════════════════════════════════════
   布局常量
   ═══════════════════════════════════════════════ */
#define SCREEN_W         1024
#define SCREEN_H         600
#define CONTAINER_PAD    16
#define ITEM_GAP         12
#define HEADER_H         60
#define FOOTER_H         52
#define PAGE_SIZE        30

/* ═══════════════════════════════════════════════
   内部状态
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_list       = NULL;
static lv_obj_t *s_page_label = NULL;
static lv_obj_t *s_prev_btn   = NULL;
static lv_obj_t *s_next_btn   = NULL;
static lv_obj_t *s_footer     = NULL;

static uint8_t       s_current_type = 0;
static game_entry_t *s_entries      = NULL;   /* 指向 DB 内部数组, 不得释放 */
static int           s_entry_count  = 0;
static int           s_current_page = 0;
static int           s_total_pages  = 0;

/* 前向声明 */
static void create_header(lv_obj_t *parent);
static void create_list_area(lv_obj_t *parent);
static void create_footer(lv_obj_t *parent);
static void populate_list(void);
static void update_page_controls(void);
static void on_back_click(lv_event_t *e);
static void on_game_click(lv_event_t *e);
static void on_prev_click(lv_event_t *e);
static void on_next_click(lv_event_t *e);

/* ═══════════════════════════════════════════════
   页面生命周期
   ═══════════════════════════════════════════════ */

void nes_game_list_ui_init(void)
{
    ESP_LOGI(TAG, "init — deferred");
}

void nes_game_list_ui_show(uint8_t type_id)
{
    /* 获取数据 */
    s_current_type = type_id;
    s_entry_count = game_db_get_games_by_type(type_id, &s_entries);
    s_total_pages = (s_entry_count + PAGE_SIZE - 1) / PAGE_SIZE;
    if (s_total_pages == 0) s_total_pages = 1;   /* 空列表仍显示 1 页 */
    s_current_page = 0;

    ESP_LOGI(TAG, "show type=%d (%s), count=%d, pages=%d",
             type_id, game_db_get_type_name(type_id),
             s_entry_count, s_total_pages);

    /* 每次显示重建屏幕 (类型/数据可能已变) */
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
        s_list       = NULL;
        s_page_label = NULL;
        s_prev_btn   = NULL;
        s_next_btn   = NULL;
        s_footer     = NULL;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);
    create_list_area(s_screen);

    /* 仅多页时显示底部导航栏 */
    if (s_total_pages > 1) {
        create_footer(s_screen);
    }

    populate_list();

    lv_screen_load(s_screen);
}

void nes_game_list_ui_hide(void)
{
    /* 由 lv_screen_load 切换; 下次 _show 会重建 */
}

/* ═══════════════════════════════════════════════
   顶部导航栏
   [← 返回]  动作冒险 (428)         第 1/15 页
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

    /* ── 返回按钮 ── */
    lv_obj_t *back_btn = lv_obj_create(header);
    lv_obj_set_size(back_btn, 100, 40);
    lv_obj_set_style_bg_color(back_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_btn, on_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(back_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(back_btn,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(back_btn, 4, 0);

    lv_obj_t *back_arrow = lv_label_create(back_btn);
    lv_label_set_text(back_arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_arrow, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_arrow, &lv_font_montserrat_14, 0);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回");
    lv_obj_set_style_text_color(back_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(back_label, g_font_cjk_14, 0);

    /* ── 标题: 分类名 + 数量 ── */
    lv_obj_t *title = lv_label_create(header);
    const char *type_name = game_db_get_type_name(s_current_type);
    lv_label_set_text_fmt(title, "%s (%d)", type_name ? type_name : "?",
                          s_entry_count);
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_20, 0);

    /* ── 页码指示器 ── */
    s_page_label = lv_label_create(header);
    lv_label_set_text_fmt(s_page_label, "第 %d/%d 页",
                          s_current_page + 1, s_total_pages);
    lv_obj_set_style_text_color(s_page_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_page_label, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   lv_list 游戏列表区域
   ═══════════════════════════════════════════════ */

static void create_list_area(lv_obj_t *parent)
{
    /* 可滑动容器包装 list */
    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_set_size(scroll, LV_PCT(100), 0);
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);

    s_list = lv_list_create(scroll);
    lv_obj_set_size(s_list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    /* 列表按钮默认字体 */
    lv_obj_set_style_text_font(s_list, g_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_list, COLOR_TEXT_PRIMARY, 0);
}

/* ═══════════════════════════════════════════════
   分页导航栏 (仅多页时显示)
   [上一页]              第 1/15 页              [下一页]
   ═══════════════════════════════════════════════ */

static void create_footer(lv_obj_t *parent)
{
    s_footer = lv_obj_create(parent);
    lv_obj_set_size(s_footer, LV_PCT(100), FOOTER_H);
    lv_obj_set_style_bg_color(s_footer, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_footer, 12, 0);
    lv_obj_set_style_pad_hor(s_footer, 20, 0);
    lv_obj_set_style_pad_ver(s_footer, 0, 0);
    lv_obj_set_style_border_width(s_footer, 0, 0);
    lv_obj_set_flex_flow(s_footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_footer,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_footer, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 上一页 ── */
    s_prev_btn = lv_obj_create(s_footer);
    lv_obj_set_size(s_prev_btn, 120, 36);
    lv_obj_set_style_bg_color(s_prev_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(s_prev_btn, 8, 0);
    lv_obj_set_style_border_width(s_prev_btn, 0, 0);
    lv_obj_add_flag(s_prev_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_prev_btn, on_prev_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(s_prev_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *prev_label = lv_label_create(s_prev_btn);
    lv_label_set_text(prev_label, "上一页");
    lv_obj_set_style_text_color(prev_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(prev_label, g_font_cjk_14, 0);
    lv_obj_center(prev_label);

    /* ── 下一页 ── */
    s_next_btn = lv_obj_create(s_footer);
    lv_obj_set_size(s_next_btn, 120, 36);
    lv_obj_set_style_bg_color(s_next_btn, COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(s_next_btn, 8, 0);
    lv_obj_set_style_border_width(s_next_btn, 0, 0);
    lv_obj_add_flag(s_next_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_next_btn, on_next_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(s_next_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *next_label = lv_label_create(s_next_btn);
    lv_label_set_text(next_label, "下一页");
    lv_obj_set_style_text_color(next_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(next_label, g_font_cjk_14, 0);
    lv_obj_center(next_label);

    /* 同步按钮状态 */
    update_page_controls();
}

/* ═══════════════════════════════════════════════
   列表填充 (核心分页逻辑)
   ═══════════════════════════════════════════════ */

static void populate_list(void)
{
    if (!s_list) return;

    /* 清空当前所有列表项 */
    lv_obj_clean(s_list);

    int start = s_current_page * PAGE_SIZE;
    int end   = start + PAGE_SIZE;
    if (end > s_entry_count) end = s_entry_count;

    if (s_entry_count == 0) {
        /* 空分类 */
        lv_obj_t *empty = lv_list_add_text(s_list, "该分类暂无游戏");
        lv_obj_set_style_text_color(empty, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(empty, g_font_cjk_16, 0);
        return;
    }

    /* 添加当前页的 30 个 (或更少) 按钮 */
    for (int i = start; i < end; i++) {
        /* 使用 NULL icon, 仅显示游戏名称 */
        lv_obj_t *btn = lv_list_add_button(s_list, NULL, s_entries[i].name);
        if (!btn) continue;

        /* 设置按钮标签字体 (list 默认 font 已设置, 但显式覆盖确保生效) */
        lv_obj_set_style_text_font(btn, g_font_cjk_16, 0);

        /* 绑定点击事件, user_data = 游戏 id */
        lv_obj_add_event_cb(btn, on_game_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)s_entries[i].id);
    }

    /* 更新页码文字 */
    if (s_page_label) {
        lv_label_set_text_fmt(s_page_label, "第 %d/%d 页",
                              s_current_page + 1, s_total_pages);
    }

    /* 更新前后翻页按钮状态 */
    update_page_controls();
}

/* ═══════════════════════════════════════════════
   翻页按钮视觉状态
   ═══════════════════════════════════════════════ */

static void update_page_controls(void)
{
    if (!s_prev_btn || !s_next_btn) return;

    /* 第一页: 上一页 变灰 */
    if (s_current_page == 0) {
        lv_obj_set_style_bg_color(s_prev_btn,
            lv_color_hex(0x1E293B), 0);   /* 接近背景色 */
        lv_obj_set_style_bg_opa(s_prev_btn, LV_OPA_50, 0);
        lv_obj_clear_flag(s_prev_btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_set_style_bg_color(s_prev_btn, COLOR_BORDER, 0);
        lv_obj_set_style_bg_opa(s_prev_btn, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_prev_btn, LV_OBJ_FLAG_CLICKABLE);
    }

    /* 最后一页: 下一页 变灰 */
    if (s_current_page >= s_total_pages - 1) {
        lv_obj_set_style_bg_color(s_next_btn,
            lv_color_hex(0x1E293B), 0);
        lv_obj_set_style_bg_opa(s_next_btn, LV_OPA_50, 0);
        lv_obj_clear_flag(s_next_btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_set_style_bg_color(s_next_btn, COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(s_next_btn, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_next_btn, LV_OBJ_FLAG_CLICKABLE);
    }
}

/* ═══════════════════════════════════════════════
   事件处理
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    LV_UNUSED(e);
    ESP_LOGI(TAG, "Back to category");
    nes_category_ui_show();
}

static void on_game_click(lv_event_t *e)
{
    uint16_t id = (uint16_t)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Game clicked: id=%d", (int)id);

    /* 获取完整 ROM 路径 */
    char rom_path[256];
    esp_err_t ret = game_db_get_rom_path(id, rom_path, sizeof(rom_path));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "get_rom_path failed for id=%d: %s",
                 (int)id, esp_err_to_name(ret));
        return;
    }

    if (rom_path[0] == '\0') {
        ESP_LOGE(TAG, "Empty ROM path for id=%d", (int)id);
        return;
    }

    /* 通过 nes_emu 加载 ROM (会创建 Core 0 NES task + 切换游戏画面) */
    ret = nes_emu_load_rom(rom_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nes_emu_load_rom failed: %s", esp_err_to_name(ret));
    }
}

static void on_prev_click(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_current_page > 0) {
        s_current_page--;
        populate_list();
        ESP_LOGI(TAG, "Page <- %d/%d", s_current_page + 1, s_total_pages);
    }
}

static void on_next_click(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_current_page < s_total_pages - 1) {
        s_current_page++;
        populate_list();
        ESP_LOGI(TAG, "Page -> %d/%d", s_current_page + 1, s_total_pages);
    }
}
