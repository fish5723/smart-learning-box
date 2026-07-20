/**
 * @file nes_category_ui.c
 * @brief NES 游戏分类选择页面 — LVGL 9.4
 *
 * 10 个分类按钮, 按 2×5 网格排列, 每按钮显示名称+游戏数量。
 * 数据来自 game_db (PSRAM 常驻索引), 不需要扫描 ROM 目录。
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md:
 *   - Dark Theme (Background=#0F172A, Card=#1E293B, Border=#334155)
 *   - 屏幕: 1024×600
 *   - 字体安全: 仅使用 ≤cjk_24 (cjk_32/36 会导致首帧卡死)
 */

#include "nes_category_ui.h"
#include "nes_game_list_ui.h"
#include "app/game_center/game_db/game_db.h"
#include "app/game_center/game_center.h"
#include "app/nes_emu/nes_emu.h"
#include "app/font_loader/font_loader.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "NES_CAT_UI";

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
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TITLE_BLUE      lv_color_hex(0x60A5FA)
#define COLOR_GOLD            lv_color_hex(0xFBBF24)
#define COLOR_DANGER          lv_color_hex(0xEF4444)

/* ═══════════════════════════════════════════════
   分类按钮颜色 (10 种, 视觉效果差异化)
   ═══════════════════════════════════════════════ */
static const lv_color_t s_cat_colors[10] = {
    LV_COLOR_MAKE(0xEF, 0x44, 0x44),  /* 0 动作冒险 — red */
    LV_COLOR_MAKE(0xF5, 0x9E, 0x0B),  /* 1 射击     — amber */
    LV_COLOR_MAKE(0x8B, 0x5C, 0xF6),  /* 2 角色扮演 — purple */
    LV_COLOR_MAKE(0x3B, 0x82, 0xF6),  /* 3 策略模拟 — blue */
    LV_COLOR_MAKE(0x10, 0xB9, 0x81),  /* 4 体育     — green */
    LV_COLOR_MAKE(0xEC, 0x48, 0x99),  /* 5 益智     — pink */
    LV_COLOR_MAKE(0x06, 0xB6, 0xD4),  /* 6 棋牌桌游 — cyan */
    LV_COLOR_MAKE(0xF9, 0x73, 0x16),  /* 7 冒险文字 — orange */
    LV_COLOR_MAKE(0x63, 0x66, 0xF1),  /* 8 教育音乐 — indigo */
    LV_COLOR_MAKE(0x64, 0x74, 0x8B),  /* 9 特殊/其他 — slate */
};

/* ═══════════════════════════════════════════════
   布局常量 (1024×600)
   ═══════════════════════════════════════════════ */
#define SCREEN_W         1024
#define SCREEN_H         600
#define CONTAINER_PAD    16
#define ITEM_GAP         12
#define HEADER_H         60

/* 2 列网格, 按钮尺寸 = 横向自动平分 */
#define CARD_W           ((SCREEN_W - CONTAINER_PAD * 2 - ITEM_GAP) / 2)  /* 490 */
#define CARD_H           88

/* ═══════════════════════════════════════════════
   全局对象
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen = NULL;

/* 前向声明 */
static void create_header(lv_obj_t *parent);
static void create_category_grid(lv_obj_t *parent);
static void create_error_state(lv_obj_t *parent);
static void on_back_click(lv_event_t *e);
static void on_category_click(lv_event_t *e);

/* ═══════════════════════════════════════════════
   页面生命周期
   ═══════════════════════════════════════════════ */

void nes_category_ui_init(void)
{
    ESP_LOGI(TAG, "init — deferred");
}

void nes_category_ui_show(void)
{
    /* 确保 DB 已加载 (不依赖 system_init 的 CONFIG_SMARTBOX_NES_EMU_ENABLED) */
    if (game_db_get_total_count() == 0) {
        ESP_LOGI(TAG, "DB not loaded, calling nes_emu_init()...");
        esp_err_t err = nes_emu_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "nes_emu_init() failed: %s — SD card may be missing",
                     esp_err_to_name(err));
        }
    }

    if (s_screen) {
        lv_screen_load(s_screen);
        return;
    }

    ESP_LOGI(TAG, "Creating category screen");

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);

    if (game_db_get_total_count() > 0) {
        create_category_grid(s_screen);
    } else {
        create_error_state(s_screen);
    }

    lv_screen_load(s_screen);
}

void nes_category_ui_hide(void)
{
    /* 页面保留在内存, 仅由 lv_screen_load 切换 */
}

/* ═══════════════════════════════════════════════
   顶部导航栏
   [← 返回]     选择游戏分类     共 1251 款
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

    /* ── 返回按钮 100×40 ── */
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

    /* ── 标题 "选择游戏分类" ── */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "选择游戏分类");
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* ── 总计数量 ── */
    lv_obj_t *count_label = lv_label_create(header);
    int total = game_db_get_total_count();
    if (total > 0) {
        lv_label_set_text_fmt(count_label, "共 %d 款", total);
    } else {
        lv_label_set_text(count_label, "加载中...");
    }
    lv_obj_set_style_text_color(count_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(count_label, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   分类按钮网格 (2 列, 带滚动)
   ═══════════════════════════════════════════════ */

static void create_category_grid(lv_obj_t *parent)
{
    /* 可滑动容器 */
    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_set_size(scroll, LV_PCT(100), 0);
    lv_obj_set_flex_grow(scroll, 1);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);

    /* 网格容器 */
    lv_obj_t *grid = lv_obj_create(scroll);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, ITEM_GAP, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    /* 构建 10 个分类按钮 */
    for (int t = 0; t < 10; t++) {
        const char *name = game_db_get_type_name((uint8_t)t);
        if (!name) continue;

        game_entry_t *list = NULL;
        int count = game_db_get_games_by_type((uint8_t)t, &list);

        /* ── 卡片容器 ── */
        lv_obj_t *card = lv_obj_create(grid);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_pad_hor(card, 20, 0);
        lv_obj_set_style_pad_ver(card, 14, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card,
            LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(card, 16, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, on_category_click, LV_EVENT_CLICKED,
                            (void *)(intptr_t)t);

        /* ── 左侧色条 ── */
        lv_obj_t *bar = lv_obj_create(card);
        lv_obj_set_size(bar, 6, 48);
        lv_obj_set_style_bg_color(bar, s_cat_colors[t], 0);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

        /* ── 文字区域 (名称 + 数量) ── */
        lv_obj_t *text_area = lv_obj_create(card);
        lv_obj_set_size(text_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(text_area, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(text_area, 0, 0);
        lv_obj_set_flex_flow(text_area, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(text_area, 6, 0);
        lv_obj_clear_flag(text_area, LV_OBJ_FLAG_SCROLLABLE);

        /* 分类名称 */
        lv_obj_t *name_label = lv_label_create(text_area);
        lv_label_set_text(name_label, name);
        lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(name_label, g_font_cjk_20, 0);

        /* 游戏数量 */
        lv_obj_t *count_label = lv_label_create(text_area);
        lv_label_set_text_fmt(count_label, "%d 款游戏", count);
        lv_obj_set_style_text_color(count_label, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(count_label, g_font_cjk_14, 0);
    }
}

/* ═══════════════════════════════════════════════
   数据库未就绪状态
   ═══════════════════════════════════════════════ */

static void create_error_state(lv_obj_t *parent)
{
    lv_obj_t *area = lv_obj_create(parent);
    lv_obj_set_size(area, LV_PCT(100), 0);
    lv_obj_set_flex_grow(area, 1);
    lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(area,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(area, 12, 0);
    lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(area);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(icon, g_font_cjk_24, 0);

    lv_obj_t *msg1 = lv_label_create(area);
    lv_label_set_text(msg1, "游戏数据库未就绪");
    lv_obj_set_style_text_color(msg1, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(msg1, g_font_cjk_18, 0);

    lv_obj_t *msg2 = lv_label_create(area);
    lv_label_set_text(msg2, "请确认 SD 卡已插入并包含 database/ 目录");
    lv_obj_set_style_text_color(msg2, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(msg2, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   事件处理
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    LV_UNUSED(e);
    ESP_LOGI(TAG, "Back to game center");
    game_center_show();
}

static void on_category_click(lv_event_t *e)
{
    uint8_t type = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Category clicked: type=%d, name=%s",
             type, game_db_get_type_name(type));
    nes_game_list_ui_show(type);
}
