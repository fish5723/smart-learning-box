/**
 * @file word_king_ui.c
 * @brief 单词王者 — LVGL 9.x 页面
 *
 * 以 math_quiz_ui.c / game2048_ui.c 为模板，遵循 UI_DESIGN_SYSTEM.md Dark Theme：
 *   - Header(60): 返回按钮 + 金色标题 + 状态
 *   - Main(flex:1): 280px 侧栏(关卡/战绩/AI提示) + 题目面板
 *   - 题目面板: “第 N 关” + 大号英文单词 + “请选择中文意思” + 2×2 选项按钮
 *   - Footer(80): 学习任务 + 奖励
 *
 * 字体：英文单词用 lv_font_montserrat_26（纯 ASCII, game2048 验证过），
 *       中文释义/标题用 g_font_cjk_* (≤24, 已验证)，规避 32/36 大字号首帧卡死。
 * 线程模型：懒加载建屏于首次 show()（游戏中心按钮事件, 已在 LVGL task 线程），
 * 反馈延时用一次性 lv_timer，全程无阻塞 delay。
 */

#include "word_king_ui.h"
#include "word_king.h"
#include "game_center.h"
#include "app/icon_loader/icon_loader.h"
#include "app/achievement/achievement.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "WORD_KING_UI";

/* ── 外部 CJK 字体（font_loader.c） ── */
extern lv_font_t *g_font_cjk_14;
extern lv_font_t *g_font_cjk_16;
extern lv_font_t *g_font_cjk_20;
extern lv_font_t *g_font_cjk_24;

/* ═══════════════════════════════════════════════
   颜色（对齐 UI_DESIGN_SYSTEM.md §2）
   ═══════════════════════════════════════════════ */
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_GOLD            lv_color_hex(0xFBBF24)
#define COLOR_SUCCESS         lv_color_hex(0x10B981)
#define COLOR_DANGER          lv_color_hex(0xEF4444)
#define COLOR_BLUE_ACCENT     lv_color_hex(0x60A5FA)
#define COLOR_AI_TEXT         lv_color_hex(0xCBD5E1)
#define COLOR_QCARD_BG        lv_color_hex(0x334155)
#define COLOR_BTN_GREEN       lv_color_hex(0x059669)  /* 单词王者主色（区别数学的蓝） */

/* ═══════════════════════════════════════════════
   布局常量（1024×600）
   ═══════════════════════════════════════════════ */
#define SCREEN_W       1024
#define SCREEN_H       600
#define CONTAINER_PAD  16
#define ITEM_GAP       12
#define HEADER_H       60
#define SIDEBAR_W      280
#define FOOTER_H       80
#define ANSWER_BTN_H   64

#define FEEDBACK_MS_OK    650
#define FEEDBACK_MS_WRONG 1200

/* ═══════════════════════════════════════════════
   UI 句柄
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen        = NULL;
static lv_obj_t *s_level_label    = NULL;
static lv_obj_t *s_score_label    = NULL;
static lv_obj_t *s_streak_label   = NULL;
static lv_obj_t *s_stat_label     = NULL;
static lv_obj_t *s_ai_box         = NULL;
static lv_obj_t *s_q_level_label  = NULL;
static lv_obj_t *s_word_label     = NULL;   /* 大号英文单词 */
static lv_obj_t *s_feedback_label = NULL;
static lv_obj_t *s_answer_btns[WORD_KING_OPTION_COUNT]   = { NULL };
static lv_obj_t *s_answer_labels[WORD_KING_OPTION_COUNT] = { NULL };

static lv_timer_t *s_pending_timer = NULL;
static bool s_locked = false;
static bool s_first_clear_awarded = false;

/* ═══════════════════════════════════════════════
   前向声明
   ═══════════════════════════════════════════════ */
static void build_screen(void);
static void create_header(lv_obj_t *parent);
static void create_main_area(lv_obj_t *parent);
static void create_sidebar(lv_obj_t *parent);
static void create_question_panel(lv_obj_t *parent);
static lv_obj_t *create_answer_button(lv_obj_t *parent, int idx);
static void create_footer(lv_obj_t *parent);

static void render_question(void);
static void refresh_sidebar(void);
static void set_button_color(int idx, lv_color_t c);

static void on_back_click(lv_event_t *e);
static void on_answer_click(lv_event_t *e);
static void schedule_advance(uint32_t delay_ms);
static void cancel_pending(void);
static void advance_timer_cb(lv_timer_t *t);

/* ═══════════════════════════════════════════════
   公共 API
   ═══════════════════════════════════════════════ */

void word_king_ui_init(void)
{
    ESP_LOGI(TAG, "word_king_ui_init() — 懒加载");
}

void word_king_ui_show(void)
{
    ESP_LOGI(TAG, "word_king_ui_show() ENTER (s_screen=%p)", (void *)s_screen);

    if (!s_screen) {
        ESP_LOGI(TAG, "  -> build_screen (first show)");
        build_screen();
        ESP_LOGI(TAG, "  -> build_screen OK (s_screen=%p)", (void *)s_screen);
    }

    cancel_pending();
    s_locked = false;
    s_first_clear_awarded = false;

    ESP_LOGI(TAG, "  -> word_king_start");
    word_king_start();

    ESP_LOGI(TAG, "  -> render_question");
    render_question();

    ESP_LOGI(TAG, "  -> refresh_sidebar");
    refresh_sidebar();

    ESP_LOGI(TAG, "  -> lv_screen_load");
    lv_screen_load(s_screen);

    ESP_LOGI(TAG, "word_king_ui_show() DONE");
}

void word_king_ui_hide(void)
{
    cancel_pending();
}

/* ═══════════════════════════════════════════════
   屏幕构建
   ═══════════════════════════════════════════════ */

static void build_screen(void)
{
    ESP_LOGI(TAG, "build_screen: create screen");
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "build_screen: header");
    create_header(s_screen);
    ESP_LOGI(TAG, "build_screen: main area");
    create_main_area(s_screen);
    ESP_LOGI(TAG, "build_screen: footer");
    create_footer(s_screen);
    ESP_LOGI(TAG, "build_screen: DONE");
}

/* ── Header ── */
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

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "单词王者");
    lv_obj_set_style_text_color(title, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    lv_obj_t *status = lv_label_create(header);
    lv_label_set_text(status, "英语单词记忆挑战");
    lv_obj_set_style_text_color(status, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(status, g_font_cjk_14, 0);
}

/* ── Main area ── */
static void create_main_area(lv_obj_t *parent)
{
    lv_obj_t *main = lv_obj_create(parent);
    lv_obj_set_size(main, LV_PCT(100), 0);
    lv_obj_set_flex_grow(main, 1);
    lv_obj_set_style_bg_opa(main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main, 0, 0);
    lv_obj_set_style_pad_all(main, 0, 0);
    lv_obj_set_style_pad_gap(main, ITEM_GAP, 0);
    lv_obj_set_flex_flow(main, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(main,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);

    ESP_LOGI(TAG, "  main: sidebar");
    create_sidebar(main);
    ESP_LOGI(TAG, "  main: question panel");
    create_question_panel(main);
    ESP_LOGI(TAG, "  main: DONE");
}

/* ── Sidebar ── */
static void create_sidebar(lv_obj_t *parent)
{
    lv_obj_t *sidebar = lv_obj_create(parent);
    lv_obj_set_size(sidebar, SIDEBAR_W, LV_PCT(100));
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sidebar, 0, 0);
    lv_obj_set_style_pad_all(sidebar, 0, 0);
    lv_obj_set_style_pad_gap(sidebar, ITEM_GAP, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    /* Card 1: 当前关卡 */
    lv_obj_t *level_card = lv_obj_create(sidebar);
    lv_obj_set_size(level_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(level_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(level_card, 16, 0);
    lv_obj_set_style_pad_all(level_card, 16, 0);
    lv_obj_set_style_border_width(level_card, 0, 0);
    lv_obj_set_flex_flow(level_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(level_card, 4, 0);
    lv_obj_clear_flag(level_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lc_title = lv_label_create(level_card);
    lv_label_set_text(lc_title, "当前关卡");
    lv_obj_set_style_text_color(lc_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lc_title, g_font_cjk_14, 0);

    s_level_label = lv_label_create(level_card);
    lv_label_set_text(s_level_label, "第 1 关");
    lv_obj_set_style_text_color(s_level_label, COLOR_BLUE_ACCENT, 0);
    lv_obj_set_style_text_font(s_level_label, g_font_cjk_24, 0);

    lv_obj_t *lc_hint = lv_label_create(level_card);
    lv_label_set_text(lc_hint, "答对进入下一关");
    lv_obj_set_style_text_color(lc_hint, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(lc_hint, g_font_cjk_14, 0);

    /* Card 2: 本局战绩 */
    lv_obj_t *score_card = lv_obj_create(sidebar);
    lv_obj_set_size(score_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(score_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(score_card, 16, 0);
    lv_obj_set_style_pad_all(score_card, 16, 0);
    lv_obj_set_style_border_width(score_card, 0, 0);
    lv_obj_set_flex_flow(score_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(score_card, 4, 0);
    lv_obj_clear_flag(score_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sc_title = lv_label_create(score_card);
    lv_label_set_text(sc_title, "本局得分");
    lv_obj_set_style_text_color(sc_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(sc_title, g_font_cjk_14, 0);

    s_score_label = lv_label_create(score_card);
    lv_label_set_text(s_score_label, "0");
    lv_obj_set_style_text_color(s_score_label, COLOR_GOLD, 0);
    lv_obj_set_style_text_font(s_score_label, g_font_cjk_24, 0);

    s_streak_label = lv_label_create(score_card);
    lv_label_set_text(s_streak_label, "连对: 0");
    lv_obj_set_style_text_color(s_streak_label, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(s_streak_label, g_font_cjk_16, 0);

    s_stat_label = lv_label_create(score_card);
    lv_label_set_text(s_stat_label, "对 0 / 错 0");
    lv_obj_set_style_text_color(s_stat_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_stat_label, g_font_cjk_14, 0);

    /* Card 3: AI 老师提示 */
    lv_obj_t *ai_card = lv_obj_create(sidebar);
    lv_obj_set_size(ai_card, LV_PCT(100), 0);
    lv_obj_set_flex_grow(ai_card, 1);
    lv_obj_set_style_bg_color(ai_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(ai_card, 16, 0);
    lv_obj_set_style_pad_all(ai_card, 16, 0);
    lv_obj_set_style_border_width(ai_card, 0, 0);
    lv_obj_set_flex_flow(ai_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(ai_card, 8, 0);
    lv_obj_clear_flag(ai_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ai_title_row = lv_obj_create(ai_card);
    lv_obj_set_size(ai_title_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ai_title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ai_title_row, 0, 0);
    lv_obj_set_style_pad_all(ai_title_row, 0, 0);
    lv_obj_set_style_pad_gap(ai_title_row, 6, 0);
    lv_obj_set_flex_flow(ai_title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ai_title_row,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ai_title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ai_icon = icon_loader_create_image(ai_title_row, ICON_ROBOT, 18, 18);
    LV_UNUSED(ai_icon);

    lv_obj_t *ai_title = lv_label_create(ai_title_row);
    lv_label_set_text(ai_title, "AI老师");
    lv_obj_set_style_text_color(ai_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(ai_title, g_font_cjk_14, 0);

    s_ai_box = lv_label_create(ai_card);
    lv_label_set_long_mode(s_ai_box, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ai_box, SIDEBAR_W - 32);
    lv_label_set_text(s_ai_box,
        "欢迎来到单词王者!\n\n"
        "看英文单词, 从四个\n"
        "选项中选出正确的\n"
        "中文意思.\n\n"
        "答对得 10 分并连击,\n"
        "词汇量越攒越多!");
    lv_obj_set_style_text_color(s_ai_box, COLOR_AI_TEXT, 0);
    lv_obj_set_style_text_font(s_ai_box, g_font_cjk_14, 0);
}

/* ── Question panel ── */
static void create_question_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 0, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_pad_gap(panel, 12, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 关卡标题 */
    s_q_level_label = lv_label_create(panel);
    lv_label_set_text(s_q_level_label, "第 1 关");
    lv_obj_set_style_text_color(s_q_level_label, COLOR_BLUE_ACCENT, 0);
    lv_obj_set_style_text_font(s_q_level_label, g_font_cjk_20, 0);

    /* 单词卡片 */
    lv_obj_t *w_card = lv_obj_create(panel);
    lv_obj_set_size(w_card, LV_PCT(100), 100);
    lv_obj_set_style_bg_color(w_card, COLOR_QCARD_BG, 0);
    lv_obj_set_style_radius(w_card, 16, 0);
    lv_obj_set_style_border_width(w_card, 0, 0);
    lv_obj_set_style_pad_all(w_card, 8, 0);
    lv_obj_set_flex_flow(w_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w_card,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(w_card, LV_OBJ_FLAG_SCROLLABLE);

    s_word_label = lv_label_create(w_card);
    lv_label_set_text(s_word_label, "word");
    lv_obj_set_style_text_color(s_word_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_word_label, &lv_font_montserrat_26, 0);

    /* 提示语 */
    lv_obj_t *tip = lv_label_create(panel);
    lv_label_set_text(tip, "请选择中文意思:");
    lv_obj_set_style_text_color(tip, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(tip, g_font_cjk_16, 0);

    /* 反馈提示（固定高度稳定布局） */
    s_feedback_label = lv_label_create(panel);
    lv_label_set_text(s_feedback_label, "");
    lv_obj_set_style_text_color(s_feedback_label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_feedback_label, g_font_cjk_20, 0);
    lv_obj_set_height(s_feedback_label, 28);

    /* 答案区：两行各两个按钮 */
    lv_obj_t *answer_area = lv_obj_create(panel);
    lv_obj_set_size(answer_area, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(answer_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(answer_area, 0, 0);
    lv_obj_set_style_pad_all(answer_area, 0, 0);
    lv_obj_set_style_pad_gap(answer_area, 12, 0);
    lv_obj_set_flex_flow(answer_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(answer_area, LV_OBJ_FLAG_SCROLLABLE);

    for (int r = 0; r < 2; r++) {
        lv_obj_t *row = lv_obj_create(answer_area);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_gap(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        for (int c = 0; c < 2; c++) {
            create_answer_button(row, r * 2 + c);
        }
    }
}

static lv_obj_t *create_answer_button(lv_obj_t *parent, int idx)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 0, ANSWER_BTN_H);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_bg_color(btn, COLOR_BTN_GREEN, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, on_answer_click, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "A");
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, g_font_cjk_24, 0);
    lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    s_answer_btns[idx]   = btn;
    s_answer_labels[idx] = label;
    return btn;
}

/* ── Footer ── */
static void create_footer(lv_obj_t *parent)
{
    lv_obj_t *footer = lv_obj_create(parent);
    lv_obj_set_size(footer, LV_PCT(100), FOOTER_H);
    lv_obj_set_style_bg_color(footer, COLOR_CARD, 0);
    lv_obj_set_style_radius(footer, 16, 0);
    lv_obj_set_style_pad_hor(footer, 20, 0);
    lv_obj_set_style_pad_ver(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *task_area = lv_obj_create(footer);
    lv_obj_set_size(task_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(task_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(task_area, 0, 0);
    lv_obj_set_flex_flow(task_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(task_area, 8, 0);
    lv_obj_clear_flag(task_area, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *task_icon = icon_loader_create_image(task_area, ICON_BOOKS, 20, 20);
    LV_UNUSED(task_icon);

    lv_obj_t *mission = lv_label_create(task_area);
    lv_label_set_text(mission, "学习任务: 积累常用英语词汇");
    lv_obj_set_style_text_color(mission, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(mission, g_font_cjk_16, 0);

    lv_obj_t *reward = lv_label_create(footer);
    lv_label_set_text(reward, "答对奖励 +25积分");
    lv_obj_set_style_text_color(reward, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(reward, g_font_cjk_20, 0);
}

/* ═══════════════════════════════════════════════
   渲染 / 刷新
   ═══════════════════════════════════════════════ */

static void render_question(void)
{
    const word_game_state_t *st = word_king_get_state();
    ESP_LOGI(TAG, "render_question: level=%d word=%s ans_idx=%d",
             st->level, word_king_get_word(), word_king_get_answer_index());

    if (s_q_level_label) {
        lv_label_set_text_fmt(s_q_level_label, "第 %d 关", st->level);
    }
    if (s_word_label) {
        lv_label_set_text(s_word_label, word_king_get_word());
    }
    if (s_feedback_label) {
        lv_label_set_text(s_feedback_label, "");
    }

    for (int i = 0; i < WORD_KING_OPTION_COUNT; i++) {
        if (s_answer_labels[i]) {
            /* “A 苹果” 形式：字母序号 + 中文释义 */
            lv_label_set_text_fmt(s_answer_labels[i], "%c  %s",
                (char)('A' + i), word_king_get_option(i));
        }
        set_button_color(i, COLOR_BTN_GREEN);
    }

    s_locked = false;
    ESP_LOGI(TAG, "render_question: DONE");
}

static void refresh_sidebar(void)
{
    const word_game_state_t *st = word_king_get_state();
    if (s_level_label)  lv_label_set_text_fmt(s_level_label, "第 %d 关", st->level);
    if (s_score_label)  lv_label_set_text_fmt(s_score_label, "%d", st->score);
    if (s_streak_label) lv_label_set_text_fmt(s_streak_label, "连对: %d", st->streak);
    if (s_stat_label)   lv_label_set_text_fmt(s_stat_label, "对 %d / 错 %d",
                                              st->correct, st->wrong);
}

static void set_button_color(int idx, lv_color_t c)
{
    if (idx >= 0 && idx < WORD_KING_OPTION_COUNT && s_answer_btns[idx]) {
        lv_obj_set_style_bg_color(s_answer_btns[idx], c, 0);
    }
}

/* ═══════════════════════════════════════════════
   事件处理
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Back to game center");
    cancel_pending();
    game_center_show();
}

static void on_answer_click(lv_event_t *e)
{
    if (s_locked) return;

    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_locked = true;

    int answer_index = word_king_get_answer_index();
    const char *correct_meaning = word_king_get_option(answer_index);

    bool correct = word_king_submit(idx);

    if (correct) {
        set_button_color(idx, COLOR_SUCCESS);
        if (s_feedback_label) {
            lv_obj_set_style_text_color(s_feedback_label, COLOR_SUCCESS, 0);
            lv_label_set_text(s_feedback_label, "回答正确!  +10 分");
        }

        /* ── 成就接入 ── */
        achievement_complete_task(ACHV_TASK_QUESTION, 1);   /* +10 EXP, 题数+1, NVS */
        const word_game_state_t *now = word_king_get_state();
        if (!s_first_clear_awarded) {
            achievement_update(25);                          /* 首次通关 +25 EXP */
            s_first_clear_awarded = true;
        }
        if (now->streak > 0 && (now->streak % 10) == 0) {
            achievement_update(15);                          /* 连对 10 题 +15 EXP */
            if (s_ai_box) {
                lv_label_set_text_fmt(s_ai_box,
                    "厉害! 连对 %d 个单词!\n\n"
                    "奖励额外 +15 经验.\n"
                    "你就是单词王者!",
                    now->streak);
            }
        } else if (s_ai_box) {
            lv_label_set_text_fmt(s_ai_box,
                "答对啦! 得分 %d.\n\n"
                "当前连对 %d 个,\n"
                "进入第 %d 关.\n"
                "继续记住更多单词!",
                now->score, now->streak, now->level);
        }

        schedule_advance(FEEDBACK_MS_OK);
    } else {
        set_button_color(idx, COLOR_DANGER);
        set_button_color(answer_index, COLOR_SUCCESS);
        if (s_feedback_label) {
            lv_obj_set_style_text_color(s_feedback_label, COLOR_DANGER, 0);
            lv_label_set_text_fmt(s_feedback_label, "答错了  正确答案: %s",
                correct_meaning);
        }
        if (s_ai_box) {
            lv_label_set_text_fmt(s_ai_box,
                "不要灰心!\n\n"
                "%s 的意思是 %s.\n"
                "连击已清零,\n"
                "记住它, 再来一个!",
                word_king_get_word(), correct_meaning);
        }
        schedule_advance(FEEDBACK_MS_WRONG);
    }

    refresh_sidebar();
}

/* ═══════════════════════════════════════════════
   反馈延时推进（一次性 lv_timer，无阻塞 delay）
   ═══════════════════════════════════════════════ */

static void schedule_advance(uint32_t delay_ms)
{
    cancel_pending();
    s_pending_timer = lv_timer_create(advance_timer_cb, delay_ms, NULL);
    lv_timer_set_repeat_count(s_pending_timer, 1);
}

static void cancel_pending(void)
{
    if (s_pending_timer) {
        lv_timer_delete(s_pending_timer);
        s_pending_timer = NULL;
    }
}

static void advance_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_pending_timer = NULL;

    word_king_next();
    render_question();
    refresh_sidebar();
}
