/**
 * @file nes_emu_ui.c
 * @brief NES 模拟器 UI 实现 — ROM 浏览器 + 游戏画面
 *
 * 两个主要页面:
 *   1. ROM 浏览器 — 网格列表浏览 TF 卡中所有 .nes 文件
 *   2. 游戏画面 — NES 帧缓冲显示 + 虚拟手柄叠加层
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md:
 *   - Dark Theme, 1024×600
 *   - 使用 font_loader 字体 + icon_loader 图标
 */

#include "nes_emu_ui.h"
#include "nes_render.h"
#include "virtual_gamepad.h"
#include "platform/nes_core.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "app/home/home.h"
#include "bsp/storage/sd_card.h"
#include "app/game_center/game_center.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "NES_UI";

/* ═══════════════════════════════════════════════
   颜色定义
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_PRIMARY         lv_color_hex(0x3B82F6)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_WARNING         lv_color_hex(0xF59E0B)
#define COLOR_DANGER          lv_color_hex(0xEF4444)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TITLE_BLUE      lv_color_hex(0x60A5FA)
#define COLOR_GOLD            lv_color_hex(0xFBBF24)
#define COLOR_BANNER_PURPLE   lv_color_hex(0x7C3AED)
#define COLOR_GAME_BG         lv_color_hex(0x000000)

/* ═══════════════════════════════════════════════
   布局常量
   ═══════════════════════════════════════════════ */
#define SCREEN_W         1024
#define SCREEN_H         600
#define CONTAINER_PAD    16
#define ITEM_GAP         12
#define HEADER_H         60

/* NES 显示区域 (2x 缩放: 256×240 → 512×480, 居中于 1024×600) */
#define NES_DISP_W        512
#define NES_DISP_H        480
#define NES_DISP_X        ((SCREEN_W - NES_DISP_W) / 2)   /* 256 */
#define NES_DISP_Y        ((SCREEN_H - HEADER_H - NES_DISP_H) / 2 + HEADER_H)  /* 居中 */

/* ROM 浏览器卡片 */
#define ROM_CARD_W        180
#define ROM_CARD_H        140
#define ROM_CARD_PAD      12
#define ROM_GRID_COLS     5

/* ═══════════════════════════════════════════════
   全局对象
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen_browser = NULL;    /* ROM 浏览器页面 */
static lv_obj_t *s_screen_game    = NULL;    /* 游戏画面页面 */
static lv_obj_t *s_rom_grid       = NULL;    /* ROM 卡片网格 */
static lv_obj_t *s_rom_name_label = NULL;    /* 当前 ROM 名称 */
static lv_obj_t *s_fps_label      = NULL;    /* FPS 显示 */

static lv_timer_t *s_frame_timer  = NULL;    /* 帧刷新定时器 */

/* 帧计数和FPS统计 */
static uint32_t   s_frame_count   = 0;
static uint32_t   s_fps           = 0;
static int64_t    s_last_fps_time = 0;

/* ROM 列表缓存 */
static nes_rom_entry_t *s_rom_list = NULL;
static int              s_rom_count = 0;

/* 前向声明 */
static void create_browser_header(lv_obj_t *parent);
static void create_rom_grid(lv_obj_t *parent);
static void on_back_click(lv_event_t *e);
static void on_rom_click(lv_event_t *e);
static void on_pause_click(lv_event_t *e);
static void create_game_header(lv_obj_t *parent);
static void create_game_header(lv_obj_t *parent);
static void create_nes_display(lv_obj_t *parent);
static void refresh_frame_cb(lv_timer_t *timer);

/* ═══════════════════════════════════════════════
   页面生命周期
   ═══════════════════════════════════════════════ */

void nes_emu_ui_init(void)
{
    ESP_LOGI(TAG, "nes_emu_ui_init()");
    /* 屏幕延迟到 nes_emu_ui_show() 时创建 */
}

void nes_emu_ui_show(void)
{
    if (s_screen_browser) {
        lv_screen_load(s_screen_browser);
        return;
    }

    ESP_LOGI(TAG, "Creating ROM browser screen");

    /* 创建 ROM 浏览器页面 */
    s_screen_browser = lv_obj_create(NULL);
    lv_obj_set_size(s_screen_browser, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen_browser, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen_browser, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen_browser, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen_browser, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen_browser, LV_OBJ_FLAG_SCROLLABLE);

    create_browser_header(s_screen_browser);
    create_rom_grid(s_screen_browser);

    lv_screen_load(s_screen_browser);
}

void nes_emu_ui_hide(void)
{
    /* 停止帧刷新 */
    if (s_frame_timer) {
        lv_timer_delete(s_frame_timer);
        s_frame_timer = NULL;
    }
}

void nes_emu_ui_show_game(const char *rom_name)
{
    ESP_LOGI(TAG, "Showing game screen: %s", rom_name);

    if (s_screen_game) {
        lv_obj_delete(s_screen_game);
    }

    /* 创建游戏画面 */
    s_screen_game = lv_obj_create(NULL);
    lv_obj_set_size(s_screen_game, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen_game, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen_game, 0, 0);
    lv_obj_clear_flag(s_screen_game, LV_OBJ_FLAG_SCROLLABLE);

    create_game_header(s_screen_game);
    create_nes_display(s_screen_game);

    /* 创建虚拟手柄叠加层 (半透明，覆盖游戏画面下方) */
    virtual_gamepad_create(s_screen_game);

    /* 显示 ROM 名称 */
    if (s_rom_name_label) {
        lv_label_set_text(s_rom_name_label, rom_name);
    }

    /* 启动帧刷新定时器 (60Hz = ~16ms) */
    if (!s_frame_timer) {
        s_frame_timer = lv_timer_create(refresh_frame_cb, 16, NULL);
    }

    lv_screen_load(s_screen_game);
}

lv_obj_t *nes_emu_ui_get_screen_area(void)
{
    return nes_render_get_canvas();
}

/* ═══════════════════════════════════════════════
   ROM 浏览器 — 头部
   [← 返回]  经典小霸王  共 N 款游戏
   ═══════════════════════════════════════════════ */

static void create_browser_header(lv_obj_t *parent)
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

    /* 返回按钮 */
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

    /* 标题 */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "经典小霸王");
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* ROM 数量 */
    lv_obj_t *count_label = lv_label_create(header);
    if (s_rom_count > 0) {
        lv_label_set_text_fmt(count_label, "共 %d 款游戏", s_rom_count);
    } else {
        lv_label_set_text(count_label, "加载中...");
    }
    lv_obj_set_style_text_color(count_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(count_label, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   ROM 浏览器 — 游戏列表网格
   ═══════════════════════════════════════════════ */

static void create_rom_grid(lv_obj_t *parent)
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
    s_rom_grid = lv_obj_create(scroll);
    lv_obj_set_size(s_rom_grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_rom_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_rom_grid, 0, 0);
    lv_obj_set_style_pad_all(s_rom_grid, 8, 0);
    lv_obj_set_style_pad_gap(s_rom_grid, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_rom_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_rom_grid,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(s_rom_grid, LV_OBJ_FLAG_SCROLLABLE);

    /* 加载 ROM 列表 */
    esp_err_t ret = nes_emu_scan_roms(&s_rom_list, &s_rom_count);
    if (ret != ESP_OK || s_rom_count == 0) {
        ESP_LOGW(TAG, "No ROMs found");

        /* 空状态提示 */
        lv_obj_t *empty = lv_label_create(s_rom_grid);
        lv_label_set_text(empty, "未找到游戏 ROM\n\n请将 .nes 文件放入 TF 卡 /roms/ 目录");
        lv_obj_set_style_text_color(empty, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_style_text_font(empty, g_font_cjk_18, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_size(empty, LV_PCT(100), LV_SIZE_CONTENT);
        return;
    }

    /* 为每个 ROM 创建卡片 */
    int display_count = s_rom_count > 50 ? 50 : s_rom_count;  /* 最多显示 50 个 */
    for (int i = 0; i < display_count; i++) {
        if (!s_rom_list[i].is_nes) continue;

        /* 卡片 */
        lv_obj_t *card = lv_obj_create(s_rom_grid);
        lv_obj_set_size(card, ROM_CARD_W, ROM_CARD_H);
        lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_pad_all(card, ROM_CARD_PAD, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card,
            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, on_rom_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        /* 图标 */
        lv_obj_t *game_icon = icon_loader_create_image(card, ICON_GAME_HANDLE, 40, 40);

        /* 名称 */
        lv_obj_t *name_label = lv_label_create(card);
        /* 截断过长的名称 */
        char display_name[64];
        strncpy(display_name, s_rom_list[i].name, sizeof(display_name) - 1);
        display_name[sizeof(display_name) - 1] = '\0';
        /* 去除 .nes 扩展名 */
        char *dot = strrchr(display_name, '.');
        if (dot) *dot = '\0';
        if (strlen(display_name) > 20) {
            display_name[18] = '.';
            display_name[19] = '.';
            display_name[20] = '\0';
        }
        lv_label_set_text(name_label, display_name);
        lv_obj_set_style_text_color(name_label, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_text_font(name_label, g_font_cjk_12, 0);
        lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(name_label, ROM_CARD_W - ROM_CARD_PAD * 2);
    }

    if (s_rom_count > 50) {
        ESP_LOGI(TAG, "Displaying 50 of %d ROMs", s_rom_count);
    } else {
        ESP_LOGI(TAG, "Displaying all %d ROMs", s_rom_count);
    }
}

static void on_rom_click(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_rom_count) return;

    ESP_LOGI(TAG, "ROM clicked: %s", s_rom_list[idx].name);

    /* 构建完整路径 */
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s", s_rom_list[idx].path);

    /* 加载并运行 ROM */
    esp_err_t ret = nes_emu_load_rom(full_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load ROM: %s", full_path);
    }
}

/* ═══════════════════════════════════════════════
   游戏画面 — 头部
   [← 退出]  ROM名称  FPS:60  [暂停] [存档]
   ═══════════════════════════════════════════════ */

static void create_game_header(lv_obj_t *parent)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), HEADER_H);
    lv_obj_set_style_bg_color(header, COLOR_CARD, 0);
    lv_obj_set_style_radius(header, 0, 0);  /* 游戏模式: 顶部无圆角 */
    lv_obj_set_style_pad_hor(header, 16, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 左侧: 退出按钮 ── */
    lv_obj_t *exit_btn = lv_obj_create(header);
    lv_obj_set_size(exit_btn, 80, 36);
    lv_obj_set_style_bg_color(exit_btn, COLOR_DANGER, 0);
    lv_obj_set_style_radius(exit_btn, 8, 0);
    lv_obj_set_style_border_width(exit_btn, 0, 0);
    lv_obj_add_flag(exit_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(exit_btn, on_back_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(exit_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *exit_label = lv_label_create(exit_btn);
    lv_label_set_text(exit_label, "退出");
    lv_obj_set_style_text_color(exit_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(exit_label, g_font_cjk_14, 0);
    lv_obj_center(exit_label);

    /* ── 中间: ROM 名称 ── */
    s_rom_name_label = lv_label_create(header);
    lv_label_set_text(s_rom_name_label, "NES");
    lv_obj_set_style_text_color(s_rom_name_label, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(s_rom_name_label, g_font_cjk_18, 0);

    /* ── 右侧: FPS + 控制按钮 ── */
    lv_obj_t *right_area = lv_obj_create(header);
    lv_obj_set_size(right_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_area, 0, 0);
    lv_obj_set_style_pad_gap(right_area, 8, 0);
    lv_obj_set_flex_flow(right_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_area,
        LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(right_area, LV_OBJ_FLAG_SCROLLABLE);

    /* FPS 标签 */
    s_fps_label = lv_label_create(right_area);
    lv_label_set_text(s_fps_label, "FPS:--");
    lv_obj_set_style_text_color(s_fps_label, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(s_fps_label, g_font_cjk_14, 0);

    /* 暂停按钮 */
    lv_obj_t *pause_btn = lv_obj_create(right_area);
    lv_obj_set_size(pause_btn, 60, 32);
    lv_obj_set_style_bg_color(pause_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(pause_btn, 8, 0);
    lv_obj_set_style_border_width(pause_btn, 0, 0);
    lv_obj_add_flag(pause_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pause_btn, on_pause_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(pause_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pause_label = lv_label_create(pause_btn);
    lv_label_set_text(pause_label, "暂停");
    lv_obj_set_style_text_color(pause_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(pause_label, g_font_cjk_12, 0);
    lv_obj_center(pause_label);
}

static void on_pause_click(lv_event_t *e)
{
    LV_UNUSED(e);
    nes_emu_toggle_pause();
}

/* ═══════════════════════════════════════════════
   NES 画面显示区域
   ═══════════════════════════════════════════════ */

static void create_nes_display(lv_obj_t *parent)
{
    /* NES 画面背景 (黑色区域) */
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_set_size(bg, NES_DISP_W + 8, NES_DISP_H + 8);
    lv_obj_set_style_bg_color(bg, COLOR_GAME_BG, 0);
    lv_obj_set_style_border_width(bg, 2, 0);
    lv_obj_set_style_border_color(bg, COLOR_BORDER, 0);
    lv_obj_set_style_radius(bg, 4, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_align(bg, LV_ALIGN_CENTER, 0, -30);  /* 稍微上移留出虚拟手柄空间 */
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* Phase 3: 创建 NES render canvas (RGB565 → RGB888, 2x 缩放) */
    nes_render_init(bg);
}

/* ═══════════════════════════════════════════════
   帧刷新回调 (60Hz)
   ═══════════════════════════════════════════════ */

static void refresh_frame_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    /* ── 等待模拟器帧就绪 (非阻塞, 最多等16ms) ── */
    if (nes_emu_is_running()) {
        extern SemaphoreHandle_t nes_emu_get_frame_ready_sem(void);
        SemaphoreHandle_t sem = nes_emu_get_frame_ready_sem();
        if (sem) {
            /* 尝试获取帧信号，不阻塞LVGL */
            if (xSemaphoreTake(sem, pdMS_TO_TICKS(2)) == pdTRUE) {
                s_frame_count++;
            }
        } else {
            /* 无信号量时直接计数 */
            s_frame_count++;
        }
    }

    /* ── FPS 更新 ── */
    if (s_fps_label) {
        int64_t now = esp_timer_get_time();
        int64_t elapsed = now - s_last_fps_time;
        if (elapsed >= 1000000) {
            s_fps = s_frame_count;
            s_frame_count = 0;
            s_last_fps_time = now;
        }
        lv_label_set_text_fmt(s_fps_label, "FPS:%lu", (unsigned long)s_fps);
    }

    /* ── 渲染帧到LVGL canvas ── */
    uint16_t *fb = nes_core_get_framebuffer();
    if (fb) {
        nes_render_update(fb);
    }
}

/* ═══════════════════════════════════════════════
   返回事件
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    LV_UNUSED(e);
    ESP_LOGI(TAG, "Back clicked");

    /* 停止模拟器 (如果正在运行) */
    nes_emu_stop();

    /* 停止帧刷新 */
    if (s_frame_timer) {
        lv_timer_delete(s_frame_timer);
        s_frame_timer = NULL;
    }

    /* 返回 ROM 浏览器（游戏列表） */
    nes_emu_ui_show();
}