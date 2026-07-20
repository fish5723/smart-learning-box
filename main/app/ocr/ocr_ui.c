/**
 * @file ocr_ui.c
 * @brief OCR 拍照解题 UI 实现 - LVGL 9.x
 *
 * 基于完善版 Screen_OCR.html 原型:
 *   - 空状态/结果状态动态切换
 *   - 扫描线动画
 *   - Toast 提示
 *   - 任务进度追踪
 *   - 知识点标签点击详情
 *   - 深入讲解追加内容
 *
 * 严格遵循 UI_DESIGN_SYSTEM.md 视觉规范:
 *   - Dark Theme (Background=#0F172A, Card=#1E293B)
 *   - 屏幕: 1024x600
 *   - 颜色: Primary=#3B82F6, Success=#10B981
 *   - 圆角: Card=16px, Button=12px
 *   - 间距: XS=4, SM=8, MD=12, LG=16, XL=24
 */

#include "ocr_ui.h"
#include "ocr_album_ui.h"
#include "home_ui.h"
#include "home.h"
#include "app/font_loader/font_loader.h"
#include "app/icon_loader/icon_loader.h"
#include "app/ai/ai_llm.h"
#include "app/achievement/achievement.h"
#include "app/photos/photo_history.h"
#include "app/wrong_book/wrong_book.h"
#include "bsp/storage/storage.h"
#include "bsp/storage/sd_card.h"
#include "bsp/camera/camera.h"
#include "manager/camera_manager.h"
#include "vision/vision_ocr.h"
#include "vision/vision_ocr_parser.h"
#include "vision/vision_core.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "OCR_UI";

/* ═══════════════════════════════════════════════
   OCR 状态机 (可读枚举)
   ═══════════════════════════════════════════════ */
typedef enum {
    OCR_IDLE,
    OCR_PREVIEW,
    OCR_CAPTURE,
    OCR_VISION,
    OCR_SOLVING,
    OCR_RESULT,
    OCR_ERROR,
} ocr_state_t;

static ocr_state_t s_ocr_state = OCR_IDLE;

/* 缓存当前 OCR 结果，供错题本按钮使用 */
static ocr_result_t s_current_result = {0};
static bool s_has_current_result = false;

static void ocr_set_state(ocr_state_t state)
{
    s_ocr_state = state;
    switch (state) {
    case OCR_IDLE:     /* 保持当前状态文字 */  break;
    case OCR_PREVIEW:  ocr_ui_set_status("摄像头已连接");    break;
    case OCR_CAPTURE:  ocr_ui_set_status("拍照中...");       break;
    case OCR_VISION:   ocr_ui_set_status("识别题目中...");   break;
    case OCR_SOLVING:  ocr_ui_set_status("AI解题中...");     break;
    case OCR_RESULT:   ocr_ui_set_status("识别完成");        break;
    case OCR_ERROR:    /* error 回调中单独设置 */            break;
    }
    ESP_LOGI(TAG, "State: %d", (int)state);
}

/* ── OCR 取消机制 ── */
static volatile bool s_cancel_requested = false;

static void ocr_cancel_request(void)
{
    if (s_ocr_state != OCR_CAPTURE && s_ocr_state != OCR_VISION
        && s_ocr_state != OCR_SOLVING) {
        return;  /* 没有进行中的 OCR 任务 */
    }
    ESP_LOGI(TAG, "Cancel requested");
    s_cancel_requested = true;
    vision_cancel();
}

/* ═══════════════════════════════════════════════
   两步流水线端点
   - Qwen3-VL (视觉识别): 看图 → 文字, 使用 CONFIG_SMARTBOX_VISION_API_KEY
   - 豆包    (解题推理): 文字 → 解答, 使用 CONFIG_SMARTBOX_LLM_API_KEY
   ═══════════════════════════════════════════════ */
static vision_endpoint_t s_vision_ep;   /* Qwen3-VL */
static vision_endpoint_t s_solver_ep;   /* 豆包 */

static void _ocr_endpoint_load(void)
{
    /* ── Vision 端点: Qwen/Qwen3-VL-8B-Instruct ──
     * Key 优先级: sdkconfig > NVS > 豆包 key 回退 */
    const char *vis_cfg_key = CONFIG_SMARTBOX_VISION_API_KEY;
    const char *vis_cfg_url = CONFIG_SMARTBOX_VISION_BASE_URL;
    const char *llm_cfg_key = CONFIG_SMARTBOX_LLM_API_KEY;

    /* URL: sdkconfig 优先, 否则用 SiliconFlow */
    if (vis_cfg_url && vis_cfg_url[0]) {
        /* 兼容: 如果用户填的是完整 URL 则直接用, 否则拼接 /chat/completions */
        static char s_vis_url[256];
        if (strstr(vis_cfg_url, "/chat/completions")) {
            s_vision_ep.api_url = vis_cfg_url;
        } else {
            snprintf(s_vis_url, sizeof(s_vis_url), "%s/chat/completions", vis_cfg_url);
            s_vision_ep.api_url = s_vis_url;
        }
    } else {
        /* 默认走 SiliconFlow OpenAI 兼容端点 (与 sdkconfig 一致)。
         * 注意: 模型名 "Qwen/Qwen3-VL-8B-Instruct" 是 SiliconFlow 命名, 必须配
         * SiliconFlow URL; 若改用 DashScope compatible-mode 需同时换成 qwen-vl-* 模型名。 */
        s_vision_ep.api_url = "https://api.siliconflow.cn/v1/chat/completions";
    }
    /* 模型以 sdkconfig 为准, 回退到 SiliconFlow 上验证过的 Qwen3-VL */
    const char *vis_cfg_model = CONFIG_SMARTBOX_VISION_MODEL;
    s_vision_ep.model = (vis_cfg_model && vis_cfg_model[0])
                        ? vis_cfg_model : "Qwen/Qwen3-VL-8B-Instruct";

    /* Key: sdkconfig Vision Key > NVS qwen key > sdkconfig LLM Key (回退) */
    static char s_vis_key_buf[128];
    if (vis_cfg_key && vis_cfg_key[0]) {
        s_vision_ep.api_key = vis_cfg_key;
    } else {
        size_t len = sizeof(s_vis_key_buf);
        if (storage_load_qwen_key(s_vis_key_buf, &len) == ESP_OK && s_vis_key_buf[0]) {
            s_vision_ep.api_key = s_vis_key_buf;
        } else if (llm_cfg_key && llm_cfg_key[0]) {
            /* 回退到豆包 Key (部分 Vision API 可复用) */
            s_vision_ep.api_key = llm_cfg_key;
        }
    }

    /* ── Solver 端点: 豆包 ──
     * Key 优先级: sdkconfig > NVS (与 ai_llm.c 一致) */
    s_solver_ep.api_url = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
    s_solver_ep.model   = "ep-20260710152809-mb4tr";

    static char s_sol_key_buf[128];
    if (llm_cfg_key && llm_cfg_key[0]) {
        s_solver_ep.api_key = llm_cfg_key;
    } else {
        size_t len = sizeof(s_sol_key_buf);
        if (storage_load_llm_key(s_sol_key_buf, &len) == ESP_OK && s_sol_key_buf[0]) {
            s_solver_ep.api_key = s_sol_key_buf;
        }
    }
}

/* ═══════════════════════════════════════════════
   消息结构 — vision_task 只投递消息, GUI Task 执行 LVGL 操作
   ═══════════════════════════════════════════════ */
typedef enum {
    OCR_MSG_STATE,
    OCR_MSG_RESULT,
    OCR_MSG_ERROR,
} ocr_msg_type_t;

typedef struct {
    ocr_msg_type_t  type;
    ocr_state_t     state;
    int             error_code;
    ocr_result_t    result;       /* 统一结果结构 */
    char            error_msg[128];
} ocr_msg_t;

/* 预览启停 (定义在下方 ocr_ui_show 附近) — 供 _ocr_msg_process 提前引用 */
static void _preview_start(void);
static void _preview_stop(void);
/* 相册导入 OCR: 因预览已在 on_album_click 停止 (非冻结), OCR 结束需主动重启预览。
 * 由 on_album_pick 置位, _ocr_msg_process (LVGL 线程) 在 RESULT/ERROR 后消费。 */
static bool s_restart_preview_after_ocr = false;

static void _ocr_msg_process(void *ctx)
{
    ocr_msg_t *msg = (ocr_msg_t *)ctx;
    if (!msg) return;

    switch (msg->type) {
    case OCR_MSG_STATE:
        ocr_set_state(msg->state);
        break;

    case OCR_MSG_RESULT: {
        ocr_result_t *r = &msg->result;

        /* ★ 调试日志: 打印完整文本内容, 帮助定位 UI 异常 */
        ESP_LOGI(TAG, "=== OCR RESULT ===");
        ESP_LOGI(TAG, "question(%u): \"%.120s\"%s",
                 (unsigned)strlen(r->question), r->question,
                 strlen(r->question) > 120 ? "..." : "");
        ESP_LOGI(TAG, "answer(%u): \"%.120s\"%s",
                 (unsigned)strlen(r->answer), r->answer,
                 strlen(r->answer) > 120 ? "..." : "");
        ESP_LOGI(TAG, "explanation(%u): \"%.120s\"%s",
                 (unsigned)strlen(r->explanation), r->explanation,
                 strlen(r->explanation) > 120 ? "..." : "");
        ESP_LOGI(TAG, "tags: %d items", r->tag_count);

        /* 缓存当前结果，供错题本按钮使用 (ocr_result_t 全部为值类型, memcpy 安全) */
        memcpy(&s_current_result, r, sizeof(ocr_result_t));
        s_has_current_result = true;

        /* 保护: question 为空显示 fallback (ocr_result_populate 已处理) */
        ocr_ui_set_question(r->question);

        /* 答案区域: ★ 先显示再设文本, 确保 flex layout 已给 label 正确宽度,
         *   LV_LABEL_LONG_WRAP 才能正确计算换行高度。先设文本再显示会导致
         *   label 在 hidden 态拿到 width=0 → 换行高度错误 → 文字错位。 */
        if (r->answer[0]) {
            ocr_ui_show_answer();          /* ★ 先显示 */
            ocr_ui_set_answer(r->answer);  /* ★ 再设文本 */
        } else {
            ocr_ui_hide_answer();
        }

        /* 解析区域 */
        ocr_ui_clear_explain();
        ocr_ui_append_explain(r->explanation[0] ? r->explanation : "暂无解析");

        /* 标签区域 */
        ocr_ui_clear_tags();
        if (r->tag_count > 0) {
            for (int i = 0; i < r->tag_count; i++) {
                ocr_ui_add_tag(r->tags[i]);
            }
            ocr_ui_show_tags();
        } else {
            ocr_ui_hide_tags();
        }

        ocr_ui_show_result();

        /* 成就接线: OCR 成功识别一次 → +经验。
         * 本回调经 lv_async_call 投递, 已运行在 LVGL 线程, 直接调用即可。
         * 护栏: question 为 fallback "未识别到题目" 时视为失败, 不发经验。 */
        if (strcmp(r->question, "未识别到题目") != 0) {
            achievement_complete_task(ACHV_TASK_QUESTION, 1);
        }
        /* 相册导入路径: OCR 结束 (成功), 重启此前停掉的实时预览。
         * 预览重启后首帧会复位 s_preview_image 的 CONTAIN 缩放 (见 s_preview_scale_dirty)。 */
        if (s_restart_preview_after_ocr) {
            s_restart_preview_after_ocr = false;
            _preview_start();
        }
        break;
    }

    case OCR_MSG_ERROR:
        ocr_ui_set_status(msg->error_msg[0] ? msg->error_msg : "识别失败, 请重试");
        /* 相册导入路径: OCR 结束 (失败), 重启此前停掉的实时预览 */
        if (s_restart_preview_after_ocr) {
            s_restart_preview_after_ocr = false;
            _preview_start();
        }
        break;
    }
    free(msg);
}

static void _ocr_send_msg(ocr_msg_t *msg)
{
    if (!msg) return;
    /* 跨线程投递: LVGL 非线程安全, lv_async_call 会操作全局 timer/async 链表,
     * 必须持适配器锁 (与 GUI 线程的 lv_timer_handler 互斥)。
     * 本函数只在 vision_task 回调中调用 (不持 freeze_mutex), 无锁序反转风险。 */
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        free(msg);
        return;
    }
    lv_result_t r = lv_async_call(_ocr_msg_process, msg);
    esp_lv_adapter_unlock();
    if (r != LV_RESULT_OK) {
        free(msg);  /* 投递失败, msg 不会被 _ocr_msg_process 释放 */
    }
}

/* ═══════════════════════════════════════════════
   Vision 回调 (在 vision_task 线程调用 — 只投递消息, 不碰 LVGL)
   ═══════════════════════════════════════════════ */
static void _on_ocr_state(vision_state_t state, void *user_data)
{
    (void)user_data;
    /* 映射 vision_state → ocr_state */
    ocr_state_t ocr_st;
    switch (state) {
    case VISION_PREPARE:  ocr_st = OCR_PREVIEW;  break;
    case VISION_CAPTURE:  ocr_st = OCR_CAPTURE;  break;
    case VISION_UPLOAD:   ocr_st = OCR_VISION;   break;
    case VISION_PROCESS:  ocr_st = OCR_SOLVING;  break;
    case VISION_DISPLAY:  ocr_st = OCR_RESULT;   break;
    case VISION_ERROR:
    case VISION_CANCELLED: ocr_st = OCR_ERROR;   break;
    default:              return;  /* CLEANUP 等不关心 */
    }

    ocr_msg_t *msg = calloc(1, sizeof(ocr_msg_t));
    if (!msg) return;
    msg->type  = OCR_MSG_STATE;
    msg->state = ocr_st;
    _ocr_send_msg(msg);
}

static void _on_ocr_result(const vision_result_t *result, void *user_data)
{
    /* 取消检查 */
    if (s_cancel_requested) {
        ESP_LOGI(TAG, "Result ignored — cancelled");
        camera_manager_preview_resume();
        return;
    }

    if (!result) {
        camera_manager_preview_resume();
        return;
    }

    /* ★ 自动保存拍照历史 (同步调用, JPEG 在此回调期间仍有效)
     * 导入的图片通常已在相册中, 跳过保存避免重复堆积。 */
    const uint8_t *jpeg_data = vision_ocr_get_jpeg_data(user_data);
    size_t jpeg_size = vision_ocr_get_jpeg_size(user_data);
    if (!vision_ocr_is_imported(user_data) && jpeg_data && jpeg_size > 0) {
        photo_history_save(jpeg_data, jpeg_size,
                          result->question, result->answer);
    }

    ocr_msg_t *msg = calloc(1, sizeof(ocr_msg_t));
    if (!msg) {
        camera_manager_preview_resume();
        return;
    }
    msg->type = OCR_MSG_RESULT;

    /* 使用统一保护接口提取结果 */
    ocr_result_populate(result, &msg->result);

    _ocr_send_msg(msg);

    /* ★ OCR 完成, 恢复实时预览 */
    camera_manager_preview_resume();
}

static void _on_ocr_error(int error_code, const char *err_msg, void *user_data)
{
    (void)user_data;
    ESP_LOGE(TAG, "OCR error 0x%04x: %s", error_code, err_msg ? err_msg : "unknown");

    ocr_msg_t *m = calloc(1, sizeof(ocr_msg_t));
    if (!m) {
        camera_manager_preview_resume();
        return;
    }
    m->type       = OCR_MSG_ERROR;
    m->error_code = error_code;
    if (err_msg) strncpy(m->error_msg, err_msg, sizeof(m->error_msg) - 1);
    _ocr_send_msg(m);

    /* ★ OCR 失败, 同样恢复预览 */
    camera_manager_preview_resume();
}

/* ═══════════════════════════════════════════════
   事件处理 (改造为调用 vision_ocr)
   ═══════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════
   颜色定义 (UI_DESIGN_SYSTEM.md S2)
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
#define COLOR_CAPTURE_BG      lv_color_hex(0x2563EB)
#define COLOR_CAPTURE_ACTIVE  lv_color_hex(0x1D4ED8)
#define COLOR_ALBUM_BG        lv_color_hex(0x475569)
#define COLOR_ALBUM_ACTIVE    lv_color_hex(0x64748B)
#define COLOR_AI_BTN_BG       lv_color_hex(0x7C3AED)
#define COLOR_AI_BTN_ACTIVE   lv_color_hex(0x6D28D9)
#define COLOR_SCAN_LINE       lv_color_hex(0x10B981)
#define COLOR_FORMULA_BG      lv_color_hex(0x334155)

/* ═══════════════════════════════════════════════
   间距定义 (UI_DESIGN_SYSTEM.md S5)
   ═══════════════════════════════════════════════ */
#define SPACING_SM  8
#define SPACING_MD  12
#define SPACING_LG  16

/* ═══════════════════════════════════════════════
   布局常量 (1024x600)
   ═══════════════════════════════════════════════ */
#define SCREEN_W         1024
#define SCREEN_H         600
#define CONTAINER_PAD    16
#define ITEM_GAP         12
#define HEADER_H         60
#define BOTTOM_H         80
#define CAMERA_PANEL_W   450
#define BTN_H            50
#define SCAN_LINE_H      3

/* ═══════════════════════════════════════════════
   任务系统常量
   ═══════════════════════════════════════════════ */
#define TASK_TARGET      3

/* ═══════════════════════════════════════════════
   全局对象句柄
   ═══════════════════════════════════════════════ */
static lv_obj_t *s_screen           = NULL;
static lv_obj_t *s_status_label     = NULL;
static lv_obj_t *s_status_dot       = NULL;
static lv_obj_t *s_camera_view      = NULL;
static lv_obj_t *s_scan_line        = NULL;
static lv_obj_t *s_result_panel     = NULL;
static lv_obj_t *s_empty_state      = NULL;
static lv_obj_t *s_ocr_card         = NULL;
static lv_obj_t *s_explain_card     = NULL;
static lv_obj_t *s_point_card       = NULL;
static lv_obj_t *s_question_label   = NULL;
static lv_obj_t *s_answer_label     = NULL;  /* ★ 答案高亮区域 */
static lv_obj_t *s_explain_label    = NULL;
static lv_obj_t *s_tag_container    = NULL;
static lv_obj_t *s_task_label       = NULL;
static lv_obj_t *s_reward_label     = NULL;
static lv_obj_t *s_toast            = NULL;
static lv_obj_t *s_placeholder      = NULL;  /* 预览区占位符 */
static lv_obj_t *s_capture_btn      = NULL;  /* 拍照/确认按钮 */
static lv_obj_t *s_capture_label    = NULL;  /* 拍照按钮文字 */
static lv_obj_t *s_album_btn        = NULL;  /* 导入/取消按钮 */
static lv_obj_t *s_album_label      = NULL;  /* 导入按钮文字 */
static lv_obj_t *s_wrongbook_btn   = NULL;  /* 标记为错题按钮 */

/* ── 实时预览 (triple buffer, 消除写/渲染撕裂) ── */
static lv_obj_t      *s_preview_image     = NULL;  /* lv_image 对象 */
static lv_image_dsc_t s_preview_dsc       = {0};   /* 图像描述符 */
static uint8_t       *s_preview_buf[3]    = { NULL, NULL, NULL };  /* PSRAM 三缓冲 */
static volatile int   s_preview_prod      = 0;     /* producer 的写入 slot — 始终 FREE */
static volatile int   s_preview_rdy       = -1;    /* 有新帧待显示的 slot (READY) */
static volatile int   s_preview_disp      = -1;    /* 当前显示中的 slot (DISPLAY) */
static volatile bool  s_preview_pending   = false; /* 有新帧待刷新 */
static lv_timer_t    *s_preview_timer     = NULL;  /* GUI 侧拉取刷新定时器 */
static bool           s_preview_active    = false;
/* 相册导入把 s_preview_image 设成了 CONTAIN 缩放; 恢复 RGB565 预览时需复位一次。
 * 由 ocr_ui_show_jpeg_file() 置位, _preview_refresh_timer_cb 处理首帧时清零。 */
static bool           s_preview_scale_dirty = false;
/* 注: s_restart_preview_after_ocr 已在文件前部声明 (供 _ocr_msg_process 引用) */

/* ═══════════════════════════════════════════════
   状态变量
   ═══════════════════════════════════════════════ */
static int s_task_count             = 0;
static bool s_has_result            = false;

/* ═══════════════════════════════════════════════
   内部函数声明
   ═══════════════════════════════════════════════ */
static void create_header(lv_obj_t *parent);
static void create_main(lv_obj_t *parent);
static void create_camera_panel(lv_obj_t *parent);
static void create_result_panel(lv_obj_t *parent);
static void create_bottom_panel(lv_obj_t *parent);
static void create_toast(lv_obj_t *parent);
static lv_obj_t *create_card(lv_obj_t *parent, const char *title);
static void show_result_cards(void);
static void hide_result_cards(void);
static void update_task_progress(void);
static void show_toast(const char *text);
static void start_scan_animation(void);
static void anim_scan_cb(lv_obj_t *obj, int32_t v);

/* Preview callbacks */
static void _on_preview_frame(const vision_blob_t *frame, void *user_data);
static void _preview_refresh_timer_cb(lv_timer_t *t);

static void on_back_click(lv_event_t *e);
static void on_capture_click(lv_event_t *e);
static void on_album_click(lv_event_t *e);
static void on_ai_detail_click(lv_event_t *e);
static void on_tag_click(lv_event_t *e);
static void on_wrongbook_click(lv_event_t *e);
static void on_btn_pressed(lv_event_t *e);
static void on_btn_released(lv_event_t *e);
static void anim_btn_scale_cb(lv_obj_t *obj, int32_t v);
static void anim_toast_opa_cb(lv_obj_t *obj, int32_t v);
static void on_scan_completed(lv_anim_t *anim);
static void on_toast_fade_completed(lv_anim_t *anim);

/* 相册导入 */
static void on_album_pick(const char *jpg_path);
static void ocr_ui_show_jpeg_file(const char *lv_src);

/* ═══════════════════════════════════════════════
   页面创建
   ═══════════════════════════════════════════════ */

static void ocr_ui_create_screen(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(s_screen, CONTAINER_PAD, 0);
    lv_obj_set_style_pad_gap(s_screen, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_header(s_screen);
    create_main(s_screen);
    create_bottom_panel(s_screen);
    create_toast(s_screen);
}

void ocr_ui_init(void)
{
    ESP_LOGI(TAG, "ocr_ui_init() — deferred");
    /* 屏幕延迟到 ocr_ui_show() 时创建 */
}

/* 启动实时预览 (GUI 线程调用): 复位双缓冲发布状态 + 起 preview task + GUI 拉取 timer。
 * 幂等: 已在预览中直接返回。 */
static void _preview_start(void)
{
    if (s_preview_active) return;
    if (!camera_manager_is_ready()) return;

    /* 复位三缓冲发布状态 (避免显示上一次会话的残留帧) */
    s_preview_pending = false;
    s_preview_rdy     = -1;
    s_preview_disp    = -1;
    s_preview_prod    = 0;

    int ret = camera_manager_preview_start(_on_preview_frame, NULL);
    if (ret == 0) {
        s_preview_active = true;
        /* GUI 侧拉取定时器: 在 GUI 线程刷新预览图, preview 线程不触碰任何 LVGL API */
        if (!s_preview_timer) {
            s_preview_timer = lv_timer_create(_preview_refresh_timer_cb, 30, NULL);
        }
        ESP_LOGI(TAG, "Preview started");
    } else {
        ESP_LOGW(TAG, "Preview start failed: 0x%x", ret);
    }
}

/* 停止实时预览 (GUI 线程调用): 停 GUI timer → 停 preview task。 */
static void _preview_stop(void)
{
    if (s_preview_timer) {
        lv_timer_delete(s_preview_timer);
        s_preview_timer = NULL;
    }
    if (s_preview_active) {
        camera_manager_preview_stop();
        s_preview_active = false;
    }
    s_preview_pending = false;
}

void ocr_ui_show(void)
{
    if (!s_screen) {
        ocr_ui_create_screen();
    }
    if (s_screen) {
        lv_screen_load(s_screen);

        /* 检查摄像头真实状态 */
        bool cam_ok = camera_manager_is_ready();
        ocr_ui_set_camera_connected(cam_ok);

        /* 启动实时预览 */
        _preview_start();
    }
}

void ocr_ui_hide(void)
{
    _preview_stop();
    ocr_ui_clear_preview();
}

/* ═══════════════════════════════════════════════
   顶部导航栏 (Height=60, Radius=16)
   [返回按钮]  [AI拍照解题]  [状态指示器]
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

    /* 返回按钮 */
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

    /* 标题 */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "AI拍照解题");
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_24, 0);

    /* 状态区域 */
    lv_obj_t *status_area = lv_obj_create(header);
    lv_obj_set_size(status_area, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(status_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_area, 0, 0);
    lv_obj_set_flex_flow(status_area, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(status_area, 6, 0);
    lv_obj_set_flex_align(status_area,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(status_area, LV_OBJ_FLAG_SCROLLABLE);

    /* 状态指示点 */
    s_status_dot = lv_obj_create(status_area);
    lv_obj_set_size(s_status_dot, 8, 8);
    lv_obj_set_style_bg_color(s_status_dot, COLOR_SUCCESS, 0);
    lv_obj_set_style_radius(s_status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_status_dot, 0, 0);
    lv_obj_clear_flag(s_status_dot, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(status_area);
    lv_label_set_text(s_status_label, "摄像头已连接");
    lv_obj_set_style_text_color(s_status_label, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(s_status_label, g_font_cjk_14, 0);
}

/* ═══════════════════════════════════════════════
   主体区域 (flex:1, row)
   左侧 Camera Panel + 右侧 Result Panel
   ═══════════════════════════════════════════════ */

static void create_main(lv_obj_t *parent)
{
    lv_obj_t *main = lv_obj_create(parent);
    lv_obj_set_size(main, LV_PCT(100), 0);
    lv_obj_set_flex_grow(main, 1);
    lv_obj_set_style_bg_opa(main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main, 0, 0);
    lv_obj_set_style_pad_all(main, 0, 0);
    lv_obj_set_style_pad_gap(main, ITEM_GAP, 0);
    lv_obj_set_flex_flow(main, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(main, LV_OBJ_FLAG_SCROLLABLE);

    create_camera_panel(main);
    create_result_panel(main);
}

/* ═══════════════════════════════════════════════
   左侧摄像头面板 (Width=450, Radius=20)
   [实时预览] [摄像头预览区+扫描线] [拍照/导入按钮]
   ═══════════════════════════════════════════════ */

static void create_camera_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, CAMERA_PANEL_W, LV_PCT(100));
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 20, 0);
    lv_obj_set_style_pad_all(panel, SPACING_LG, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(panel, SPACING_MD, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 面板标题 */
    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "实时预览");
    lv_obj_set_style_text_color(title, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_20, 0);

    /* 摄像头预览区 */
    s_camera_view = lv_obj_create(panel);
    lv_obj_set_size(s_camera_view, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_camera_view, 1);
    lv_obj_set_style_bg_color(s_camera_view, COLOR_BORDER, 0);
    lv_obj_set_style_radius(s_camera_view, 16, 0);
    lv_obj_set_style_border_width(s_camera_view, 0, 0);
    lv_obj_set_style_pad_all(s_camera_view, 0, 0);
    lv_obj_clear_flag(s_camera_view, LV_OBJ_FLAG_SCROLLABLE);

    /* 占位提示 */
    lv_obj_t *placeholder = lv_obj_create(s_camera_view);
    lv_obj_set_size(placeholder, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(placeholder, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(placeholder, 0, 0);
    lv_obj_set_flex_flow(placeholder, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(placeholder, SPACING_MD, 0);
    lv_obj_set_flex_align(placeholder,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(placeholder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(placeholder);
    s_placeholder = placeholder;  /* 保存引用，拍照后隐藏 */

    lv_obj_t *hint1 = lv_label_create(placeholder);
    lv_label_set_text(hint1, "摄像头预览区");
    lv_obj_set_style_text_color(hint1, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint1, g_font_cjk_20, 0);

    lv_obj_t *hint2 = lv_label_create(placeholder);
    lv_label_set_text(hint2, "OV5647 MIPI-CSI");
    lv_obj_set_style_text_color(hint2, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(hint2, g_font_cjk_14, 0);

    /* 预览图像 (初始隐藏, 实时预览时显示) */
    s_preview_image = lv_image_create(s_camera_view);
    lv_obj_set_size(s_preview_image, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(s_preview_image, 16, 0);
    lv_obj_add_flag(s_preview_image, LV_OBJ_FLAG_HIDDEN);

    /* 扫描线 (初始隐藏) */
    s_scan_line = lv_obj_create(s_camera_view);
    lv_obj_set_size(s_scan_line, LV_PCT(100), SCAN_LINE_H);
    lv_obj_set_style_bg_color(s_scan_line, COLOR_SCAN_LINE, 0);
    lv_obj_set_style_radius(s_scan_line, 0, 0);
    lv_obj_set_style_border_width(s_scan_line, 0, 0);
    lv_obj_set_style_shadow_width(s_scan_line, 10, 0);
    lv_obj_set_style_shadow_color(s_scan_line, COLOR_SCAN_LINE, 0);
    lv_obj_align(s_scan_line, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(s_scan_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_scan_line, LV_OBJ_FLAG_SCROLLABLE);

    /* 按钮行 */
    lv_obj_t *btn_row = lv_obj_create(panel);
    lv_obj_set_size(btn_row, LV_PCT(100), BTN_H);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_style_pad_gap(btn_row, SPACING_MD, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    /* 拍照识别按钮 */
    lv_obj_t *capture_btn = lv_obj_create(btn_row);
    lv_obj_set_size(capture_btn, 0, BTN_H);
    lv_obj_set_flex_grow(capture_btn, 1);
    lv_obj_set_style_bg_color(capture_btn, COLOR_CAPTURE_BG, 0);
    lv_obj_set_style_radius(capture_btn, 12, 0);
    lv_obj_set_style_border_width(capture_btn, 0, 0);
    lv_obj_add_flag(capture_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(capture_btn, on_capture_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(capture_btn, on_btn_pressed, LV_EVENT_PRESSED, (void *)(uintptr_t)0x1D4ED8);
    lv_obj_add_event_cb(capture_btn, on_btn_released, LV_EVENT_RELEASED, (void *)(uintptr_t)0x2563EB);
    lv_obj_clear_flag(capture_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(capture_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(capture_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(capture_btn, 8, 0);

    lv_obj_t *capture_icon = icon_loader_create_image(capture_btn, ICON_FLASH_CAMERA, 24, 24);
    LV_UNUSED(capture_icon);

    lv_obj_t *capture_label = lv_label_create(capture_btn);
    lv_label_set_text(capture_label, "拍照识别");
    lv_obj_set_style_text_color(capture_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(capture_label, g_font_cjk_20, 0);

    s_capture_btn   = capture_btn;
    s_capture_label = capture_label;

    /* 导入图片按钮 (预览时变为取消) */
    lv_obj_t *album_btn = lv_obj_create(btn_row);
    lv_obj_set_size(album_btn, 0, BTN_H);
    lv_obj_set_flex_grow(album_btn, 1);
    lv_obj_set_style_bg_color(album_btn, COLOR_ALBUM_BG, 0);
    lv_obj_set_style_radius(album_btn, 12, 0);
    lv_obj_set_style_border_width(album_btn, 0, 0);
    lv_obj_add_flag(album_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(album_btn, on_album_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(album_btn, on_btn_pressed, LV_EVENT_PRESSED, (void *)(uintptr_t)0x64748B);
    lv_obj_add_event_cb(album_btn, on_btn_released, LV_EVENT_RELEASED, (void *)(uintptr_t)0x475569);
    lv_obj_clear_flag(album_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *album_label = lv_label_create(album_btn);
    lv_label_set_text(album_label, "导入图片");
    lv_obj_set_style_text_color(album_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(album_label, g_font_cjk_20, 0);
    lv_obj_center(album_label);

    s_album_btn   = album_btn;
    s_album_label = album_label;
}

/* ═══════════════════════════════════════════════
   右侧结果面板 (flex:1, column)
   空状态 / [OCR识别结果] [AI老师讲解] [知识点]
   ═══════════════════════════════════════════════ */

static void create_result_panel(lv_obj_t *parent)
{
    s_result_panel = lv_obj_create(parent);
    lv_obj_set_size(s_result_panel, 0, LV_PCT(100));
    lv_obj_set_flex_grow(s_result_panel, 1);
    lv_obj_set_style_bg_opa(s_result_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_result_panel, 0, 0);
    lv_obj_set_style_pad_all(s_result_panel, 0, 0);
    lv_obj_set_style_pad_gap(s_result_panel, ITEM_GAP, 0);
    lv_obj_set_flex_flow(s_result_panel, LV_FLEX_FLOW_COLUMN);
    /* ★ 启用垂直滚动: 长文本不再溢出屏幕, 内容可滚动浏览 */
    lv_obj_add_flag(s_result_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_result_panel, LV_DIR_VER);

    /* ── 空状态 (默认显示) ── */
    s_empty_state = lv_obj_create(s_result_panel);
    lv_obj_set_size(s_empty_state, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_empty_state, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_empty_state, 16, 0);
    lv_obj_set_style_border_width(s_empty_state, 0, 0);
    lv_obj_set_flex_flow(s_empty_state, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_empty_state, SPACING_MD, 0);
    lv_obj_set_flex_align(s_empty_state,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_empty_state, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *empty_icon = icon_loader_create_image(s_empty_state, ICON_FLASH_CAMERA, 72, 72);
    LV_UNUSED(empty_icon);

    lv_obj_t *empty_title = lv_label_create(s_empty_state);
    lv_label_set_text(empty_title, "拍摄或导入题目");
    lv_obj_set_style_text_color(empty_title, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(empty_title, g_font_cjk_20, 0);

    lv_obj_t *empty_desc = lv_label_create(s_empty_state);
    lv_label_set_text(empty_desc, "AI 将自动识别并讲解");
    lv_obj_set_style_text_color(empty_desc, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(empty_desc, g_font_cjk_14, 0);

    /* ── OCR 识别结果卡片 (初始隐藏) ── */
    s_ocr_card = create_card(s_result_panel, "OCR识别结果");
    lv_obj_add_flag(s_ocr_card, LV_OBJ_FLAG_HIDDEN);

    s_question_label = lv_label_create(s_ocr_card);
    lv_label_set_text(s_question_label, "");
    lv_obj_set_width(s_question_label, LV_PCT(100));    /* ★ LVGL9 flex 无 STRETCH, 需显式宽度才能 WRAP */
    lv_obj_set_style_text_color(s_question_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_question_label, g_font_cjk_20, 0);
    lv_label_set_long_mode(s_question_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_question_label, 6, 0);  /* ★ 中文字符行间距 */

    /* ── 答案高亮区域 (初始隐藏) ──
     * ★ 关键: LVGL9 flex 无 STRETCH 跨轴对齐, 必须 lv_obj_set_width(LV_PCT(100))
     *    让 label 获得正确宽度 → LV_LABEL_LONG_WRAP 才能正确换行。 */
    s_answer_label = lv_label_create(s_ocr_card);
    lv_label_set_text(s_answer_label, "");
    lv_obj_set_width(s_answer_label, LV_PCT(100));      /* ★ 显式宽度: 保证 WRAP 正确换行 */
    lv_obj_set_style_text_color(s_answer_label, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(s_answer_label, g_font_cjk_24, 0);
    lv_label_set_long_mode(s_answer_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_answer_label, 6, 0);  /* ★ 中文字符行间距 */
    lv_obj_set_style_bg_color(s_answer_label, lv_color_hex(0x064E3B), 0);  /* 深绿底 */
    lv_obj_set_style_radius(s_answer_label, 8, 0);
    lv_obj_set_style_pad_all(s_answer_label, SPACING_MD, 0);
    lv_obj_add_flag(s_answer_label, LV_OBJ_FLAG_HIDDEN);

    /* ── AI 老师讲解卡片 (初始隐藏, 可滚动, 最小高度保证可见性) ── */
    s_explain_card = lv_obj_create(s_result_panel);
    lv_obj_set_size(s_explain_card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(s_explain_card, 120, 0);   /* 最小高度, 内容少时不至于消失 */
    lv_obj_set_style_bg_color(s_explain_card, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_explain_card, 16, 0);
    lv_obj_set_style_pad_all(s_explain_card, SPACING_LG, 0);
    lv_obj_set_style_border_width(s_explain_card, 0, 0);
    lv_obj_set_flex_flow(s_explain_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(s_explain_card, SPACING_MD, 0);
    lv_obj_add_flag(s_explain_card, LV_OBJ_FLAG_HIDDEN);
    /* 增加滚动能力 (讲解文本超长时在此卡片内滚动) */
    lv_obj_add_flag(s_explain_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_explain_card, LV_DIR_VER);

    lv_obj_t *explain_title = lv_label_create(s_explain_card);
    lv_label_set_text(explain_title, "AI老师讲解");
    lv_obj_set_style_text_color(explain_title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(explain_title, g_font_cjk_20, 0);

    s_explain_label = lv_label_create(s_explain_card);
    lv_label_set_text(s_explain_label, "");
    lv_obj_set_width(s_explain_label, LV_PCT(100));       /* ★ LVGL9 flex 无 STRETCH, 需显式宽度 */
    lv_obj_set_style_text_color(s_explain_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_explain_label, g_font_cjk_16, 0);
    lv_label_set_long_mode(s_explain_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(s_explain_label, 4, 0);  /* ★ 中文字符行间距 */

    /* ── 知识点卡片 (初始隐藏, 自适应高度, 标签自动换行) ── */
    s_point_card = create_card(s_result_panel, "知识点");
    lv_obj_add_flag(s_point_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flex_flow(s_point_card, LV_FLEX_FLOW_ROW_WRAP);  /* 标签自动换行 */
    lv_obj_set_style_pad_gap(s_point_card, SPACING_SM, 0);
    s_tag_container = s_point_card;
}

/* ═══════════════════════════════════════════════
   通用卡片创建 (背景 Card, 圆角 16, padding 16)
   ═══════════════════════════════════════════════ */

static lv_obj_t *create_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, SPACING_LG, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(card, SPACING_MD, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title_label, g_font_cjk_20, 0);

    return card;
}

/* ═══════════════════════════════════════════════
   底部任务栏 (Height=80, Radius=16)
   [今日任务进度] [积分奖励] [深入讲解]
   ═══════════════════════════════════════════════ */

static void create_bottom_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LV_PCT(100), BOTTOM_H);
    lv_obj_set_style_bg_color(panel, COLOR_CARD, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_hor(panel, 20, 0);
    lv_obj_set_style_pad_ver(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* 今日任务进度 */
    s_task_label = lv_label_create(panel);
    lv_label_set_text(s_task_label, "今日任务: 完成0次/3次拍照解题");
    lv_obj_set_style_text_color(s_task_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(s_task_label, g_font_cjk_16, 0);

    /* 积分奖励 */
    s_reward_label = lv_label_create(panel);
    lv_label_set_text(s_reward_label, "+10 学习积分");
    lv_obj_set_style_text_color(s_reward_label, COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(s_reward_label, g_font_cjk_24, 0);

    /* 标记为错题按钮 */
    lv_obj_t *wrongbook_btn = lv_obj_create(panel);
    lv_obj_set_size(wrongbook_btn, 180, BTN_H);
    lv_obj_set_style_bg_color(wrongbook_btn, COLOR_DANGER, 0);
    lv_obj_set_style_radius(wrongbook_btn, 12, 0);
    lv_obj_set_style_border_width(wrongbook_btn, 0, 0);
    lv_obj_add_flag(wrongbook_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wrongbook_btn, on_wrongbook_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(wrongbook_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(wrongbook_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrongbook_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(wrongbook_btn, 6, 0);

    lv_obj_t *wrongbook_icon = icon_loader_create_image(wrongbook_btn, ICON_PENCIL, 22, 22);
    LV_UNUSED(wrongbook_icon);

    lv_obj_t *wrongbook_label = lv_label_create(wrongbook_btn);
    lv_label_set_text(wrongbook_label, "标记为错题");
    lv_obj_set_style_text_color(wrongbook_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(wrongbook_label, g_font_cjk_20, 0);

    s_wrongbook_btn = wrongbook_btn;

    /* 深入讲解按钮 */
    lv_obj_t *ai_btn = lv_obj_create(panel);
    lv_obj_set_size(ai_btn, 180, BTN_H);
    lv_obj_set_style_bg_color(ai_btn, COLOR_AI_BTN_BG, 0);
    lv_obj_set_style_radius(ai_btn, 12, 0);
    lv_obj_set_style_border_width(ai_btn, 0, 0);
    lv_obj_add_flag(ai_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ai_btn, on_ai_detail_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ai_btn, on_btn_pressed, LV_EVENT_PRESSED, (void *)(uintptr_t)0x6D28D9);
    lv_obj_add_event_cb(ai_btn, on_btn_released, LV_EVENT_RELEASED, (void *)(uintptr_t)0x7C3AED);
    lv_obj_clear_flag(ai_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ai_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ai_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ai_btn, 6, 0);

    lv_obj_t *ai_icon = icon_loader_create_image(ai_btn, ICON_LIGHT_BULB, 22, 22);
    LV_UNUSED(ai_icon);

    lv_obj_t *ai_label = lv_label_create(ai_btn);
    lv_label_set_text(ai_label, "深入讲解");
    lv_obj_set_style_text_color(ai_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(ai_label, g_font_cjk_20, 0);
}

/* ═══════════════════════════════════════════════
   Toast 提示 (居中浮动, 2秒后自动消失)
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

/* ═══════════════════════════════════════════════
   扫描线动画
   ═══════════════════════════════════════════════ */

static void start_scan_animation(void)
{
    if (!s_scan_line) return;

    lv_obj_clear_flag(s_scan_line, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_scan_line);
    lv_anim_set_values(&a, 0, lv_obj_get_height(s_camera_view) - SCAN_LINE_H);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_scan_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, on_scan_completed);
    lv_anim_start(&a);
}

static void anim_scan_cb(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_y(obj, v);
}

/* ═══════════════════════════════════════════════
   结果显示/隐藏控制
   ═══════════════════════════════════════════════ */

static void show_result_cards(void)
{
    if (s_empty_state) lv_obj_add_flag(s_empty_state, LV_OBJ_FLAG_HIDDEN);
    if (s_ocr_card) lv_obj_clear_flag(s_ocr_card, LV_OBJ_FLAG_HIDDEN);
    if (s_explain_card) lv_obj_clear_flag(s_explain_card, LV_OBJ_FLAG_HIDDEN);
    if (s_point_card) lv_obj_clear_flag(s_point_card, LV_OBJ_FLAG_HIDDEN);
    s_has_result = true;
}

void ocr_ui_show_result(void)
{
    show_result_cards();
    /* ★ 强制刷新布局: 标签文本设完后手动触发布局重算, 确保:
     *   - s_ocr_card (LV_SIZE_CONTENT) 高度随内容自适应
     *   - s_explain_card 高度正确
     *   - 滚动区域 content size 准确 */
    if (s_result_panel) {
        lv_obj_update_layout(s_result_panel);
    }
}

/* ═══════════════════════════════════════════════
   实时预览回调 + 帧显示
   ═══════════════════════════════════════════════ */

/**
 * @brief Camera Manager 预览帧回调 (在 preview task 线程调用)
 *
 * ISP 输出已是 RGB565 LE (camera.c flags.byte_swap_en=1),
 * 直接 memcpy → 发布, 无需软件字节交换。
 * 本函数不调用任何 LVGL API。
 */
static void _on_preview_frame(const vision_blob_t *frame, void *user_data)
{
    (void)user_data;
    if (!frame || !frame->data) return;

    int prod = s_preview_prod;

    /* 惰性分配三缓冲 (PSRAM) */
    if (!s_preview_buf[prod]) {
        s_preview_buf[prod] = heap_caps_malloc(frame->len,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_preview_buf[prod]) {
            ESP_LOGE(TAG, "preview: buf[%d] alloc failed (%u bytes)",
                     prod, (unsigned)frame->len);
            return;
        }
        ESP_LOGI(TAG, "preview: buf[%d] allocated (%u bytes, PSRAM)",
                 prod, (unsigned)frame->len);
    }

    /* ISP 已输出 RGB565 LE, 直接拷贝即可 */
    memcpy(s_preview_buf[prod], frame->data, frame->len);

    /* 发布: 标记 READY, 通知 LVGL timer */
    s_preview_rdy    = prod;
    s_preview_pending = true;

    /* 推进 prod 到下一个 FREE slot (≠ READY 且 ≠ DISPLAY).
     * 三个 slot 最多两个被占用 (1 READY + 1 DISPLAY), 总有一个 FREE. */
    int next = (prod + 1) % 3;
    if (next == s_preview_rdy || next == s_preview_disp) {
        next = (next + 1) % 3;
    }
    s_preview_prod = next;
}

/**
 * @brief GUI 线程拉取刷新 (lv_timer, 已在 LVGL 锁内)
 *
 * 读走 READY 状态的 slot, 标记 DISPLAY 后发布给 LVGL。
 * 同时旧 DISPLAY slot 隐式变为 FREE — producer 因其 ≠ READY 且 ≠ DISPLAY
 * 而会自动选中它。
 */
static void _preview_refresh_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_preview_pending) return;
    s_preview_pending = false;

    int r = s_preview_rdy;
    if (r < 0 || !s_preview_buf[r] || !s_preview_image) return;

    int cam_w = camera_get_width();
    int cam_h = camera_get_height();

    s_preview_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_preview_dsc.header.w      = cam_w;
    s_preview_dsc.header.h      = cam_h;
    s_preview_dsc.header.stride = cam_w * 2;
    s_preview_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;  /* LVGL 9 必须: 否则被当文件路径解码 */
    s_preview_dsc.data          = s_preview_buf[r];
    s_preview_dsc.data_size     = (uint32_t)(cam_w * cam_h * 2);

    lv_image_set_src(s_preview_image, &s_preview_dsc);

    /* √ 发布为 DISPLAY — producer 写入时跳过此 slot */
    s_preview_disp = r;

    /* ★ 复位相册导入残留的缩放状态 (CONTAIN + 非 256 scale):
     *   导入图片复用同一 s_preview_image 且设过 LV_IMAGE_ALIGN_CONTAIN。
     *   OCR 完成 resume 后图片仍可见 (非 HIDDEN 分支), 故用脏标记单独处理:
     *   若不复位, RGB565 预览每帧走软件缩放 → FPS 下降。仅在导入后复位一次。 */
    if (s_preview_scale_dirty) {
        s_preview_scale_dirty = false;
        lv_image_set_inner_align(s_preview_image, LV_IMAGE_ALIGN_DEFAULT);
        lv_image_set_scale(s_preview_image, 256);
    }

    /* 首次帧到达: 显示预览, 隐藏占位符 (先 show 再 invalidate, 确保重绘正确) */
    if (lv_obj_has_flag(s_preview_image, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(s_preview_image, LV_OBJ_FLAG_HIDDEN);
        if (s_placeholder) {
            lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ocr_ui_show_camera_frame(const vision_blob_t *frame)
{
    /* 手动帧显示 (非预览模式): 填充双缓冲后立即刷新。须在 GUI 线程调用。 */
    _on_preview_frame(frame, NULL);
    _preview_refresh_timer_cb(NULL);
}

void ocr_ui_clear_preview(void)
{
    if (s_preview_image) {
        lv_obj_add_flag(s_preview_image, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_placeholder) {
        lv_obj_clear_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
    /* 不释放 s_preview_buf[] — 下次预览时复用 */
}

/* ═══════════════════════════════════════════════
   任务进度更新
   ═══════════════════════════════════════════════ */

static void update_task_progress(void)
{
    if (!s_task_label || !s_reward_label) return;

    char buf[48];
    lv_snprintf(buf, sizeof(buf), "今日任务: 完成%d次/%d次拍照解题", s_task_count, TASK_TARGET);
    lv_label_set_text(s_task_label, buf);

    if (s_task_count >= TASK_TARGET) {
        lv_label_set_text(s_reward_label, "+30 学习积分");
        lv_label_set_text(s_task_label, "今日任务已完成!");
    }
}

/* ═══════════════════════════════════════════════
   Toast 显示
   ═══════════════════════════════════════════════ */

static void show_toast(const char *text)
{
    if (!s_toast) return;

    lv_obj_t *label = lv_obj_get_child(s_toast, 0);
    if (label) lv_label_set_text(label, text);

    lv_obj_set_style_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);

    /* 2秒后隐藏 */
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
   外部更新接口
   ═══════════════════════════════════════════════ */

void ocr_ui_set_status(const char *text)
{
    if (s_status_label && text) {
        lv_label_set_text(s_status_label, text);
    }
}

void ocr_ui_set_camera_connected(bool connected)
{
    if (!s_status_dot || !s_status_label) return;

    if (connected) {
        lv_obj_set_style_bg_color(s_status_dot, COLOR_SUCCESS, 0);
        lv_label_set_text(s_status_label, "摄像头已连接");
        lv_obj_set_style_text_color(s_status_label, COLOR_SUCCESS, 0);
    } else {
        lv_obj_set_style_bg_color(s_status_dot, COLOR_DANGER, 0);
        lv_label_set_text(s_status_label, "摄像头未连接");
        lv_obj_set_style_text_color(s_status_label, COLOR_DANGER, 0);
    }
}

void ocr_ui_set_question(const char *text)
{
    if (s_question_label && text) {
        lv_label_set_text(s_question_label, text);
        /* ★ 调试日志: 打印 label 实际尺寸/位置, 帮助定位布局问题 */
        ESP_LOGI(TAG, "QUESTION_LABEL: text_len=%u x=%d y=%d w=%d h=%d",
                 (unsigned)strlen(text),
                 lv_obj_get_x(s_question_label), lv_obj_get_y(s_question_label),
                 lv_obj_get_width(s_question_label), lv_obj_get_height(s_question_label));
    }
}

void ocr_ui_set_explain(const char *text)
{
    if (s_explain_label && text) {
        lv_label_set_text(s_explain_label, text);
        ESP_LOGI(TAG, "EXPLAIN_LABEL: text_len=%u x=%d y=%d w=%d h=%d",
                 (unsigned)strlen(text),
                 lv_obj_get_x(s_explain_label), lv_obj_get_y(s_explain_label),
                 lv_obj_get_width(s_explain_label), lv_obj_get_height(s_explain_label));
    }
}

void ocr_ui_clear_explain(void)
{
    if (s_explain_label) {
        lv_label_set_text(s_explain_label, "");
    }
}

void ocr_ui_append_explain(const char *text)
{
    if (!s_explain_label || !text) return;

    const char *cur = lv_label_get_text(s_explain_label);
    size_t len = strlen(cur) + strlen(text) + 2;
    char *buf = lv_malloc(len);
    if (!buf) return;

    lv_snprintf(buf, len, "%s%s", cur, text);
    lv_label_set_text(s_explain_label, buf);
    lv_free(buf);
}

void ocr_ui_add_tag(const char *text)
{
    if (!s_tag_container || !text) return;

    /* 估算像素宽度: UTF-8 中文字符 ≈ 14px/char, 加 padding */
    int char_cnt = 0;
    for (const char *p = text; *p; p++) {
        if ((*p & 0xC0) != 0x80) char_cnt++;  /* UTF-8 首字节计数 */
    }
    int tag_w = char_cnt * 14 + SPACING_MD * 2;
    if (tag_w < 40) tag_w = 40;

    lv_obj_t *tag = lv_obj_create(s_tag_container);
    lv_obj_set_size(tag, tag_w, 36);
    lv_obj_set_style_bg_color(tag, COLOR_CAPTURE_BG, 0);
    lv_obj_set_style_radius(tag, 20, 0);
    lv_obj_set_style_pad_hor(tag, SPACING_MD, 0);
    lv_obj_set_style_pad_ver(tag, 4, 0);
    lv_obj_set_style_border_width(tag, 0, 0);
    lv_obj_add_flag(tag, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tag, on_tag_click, LV_EVENT_CLICKED, NULL);
    lv_obj_set_flex_flow(tag, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tag, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tag, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(tag);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(label, g_font_cjk_16, 0);
}

void ocr_ui_clear_tags(void)
{
    if (!s_tag_container) return;

    uint32_t cnt = lv_obj_get_child_cnt(s_tag_container);
    for (uint32_t i = cnt; i > 1; i--) {
        lv_obj_t *child = lv_obj_get_child(s_tag_container, i - 1);
        if (child) lv_obj_delete(child);
    }
}

/* ── Answer visibility ── */
void ocr_ui_set_answer(const char *text)
{
    if (s_answer_label && text) {
        lv_label_set_text(s_answer_label, text);
        ESP_LOGI(TAG, "ANSWER_LABEL: text_len=%u x=%d y=%d w=%d h=%d",
                 (unsigned)strlen(text),
                 lv_obj_get_x(s_answer_label), lv_obj_get_y(s_answer_label),
                 lv_obj_get_width(s_answer_label), lv_obj_get_height(s_answer_label));
    }
}

void ocr_ui_show_answer(void)
{
    if (s_answer_label) lv_obj_clear_flag(s_answer_label, LV_OBJ_FLAG_HIDDEN);
}

void ocr_ui_hide_answer(void)
{
    if (s_answer_label) lv_obj_add_flag(s_answer_label, LV_OBJ_FLAG_HIDDEN);
}

/* ── Tags visibility ── */
void ocr_ui_show_tags(void)
{
    if (s_point_card) lv_obj_clear_flag(s_point_card, LV_OBJ_FLAG_HIDDEN);
}

void ocr_ui_hide_tags(void)
{
    if (s_point_card) lv_obj_add_flag(s_point_card, LV_OBJ_FLAG_HIDDEN);
}

void ocr_ui_scroll_explain_to_bottom(void)
{
    if (s_explain_card) {
        lv_obj_update_layout(s_explain_card);
        lv_obj_scroll_to_y(s_explain_card, LV_COORD_MAX, LV_ANIM_ON);
    }
}

/* ═══════════════════════════════════════════════
   事件处理
   ═══════════════════════════════════════════════ */

static void on_back_click(lv_event_t *e)
{
    (void)e;
    /* 如有进行中的 OCR 任务, 先取消 */
    ocr_cancel_request();
    /* 停止预览, 释放 buffer (不销毁 screen, 保持页面缓存) */
    ocr_ui_hide();
    ESP_LOGI(TAG, "Back to home");
    home_show();
}

static void on_capture_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Capture clicked — freezing preview + starting OCR pipeline");

    if (vision_is_busy()) {
        show_toast("任务进行中，请稍候");
        return;
    }

    s_cancel_requested = false;  /* 新任务, 清除取消标记 */

    if (!s_preview_active) {
        ESP_LOGE(TAG, "Preview not active — cannot capture");
        show_toast("摄像头未就绪，请稍后重试");
        return;
    }

    if (!s_vision_ep.api_url) {
        _ocr_endpoint_load();
    }

    /* 检查双端点配置 */
    if (!s_vision_ep.api_key) {
        ESP_LOGE(TAG, "Qwen API Key not configured");
        show_toast("请先配置通义千问 API Key");
        return;
    }
    if (!s_solver_ep.api_key) {
        ESP_LOGE(TAG, "Doubao API Key not configured");
        show_toast("请先配置豆包 API Key");
        return;
    }

    /* ★ 冻结预览画面 (当前帧转移给 OCR pipeline) */
    int freeze_ret = camera_manager_preview_freeze();
    if (freeze_ret != 0) {
        ESP_LOGE(TAG, "Preview freeze failed: 0x%x", freeze_ret);
        show_toast("拍照失败，请重试");
        return;
    }

    vision_callback_t cb = {
        .on_state    = _on_ocr_state,
        .on_progress = NULL,
        .on_result   = _on_ocr_result,
        .on_error    = _on_ocr_error,
    };

    int ret = vision_ocr_start(&s_vision_ep, &s_solver_ep, &cb, NULL);
    if (ret != 0) {
        ESP_LOGE(TAG, "vision_ocr_start failed: 0x%x", ret);
        camera_manager_preview_resume();  /* 启动失败, 恢复预览 */
        show_toast("启动失败，请检查摄像头");
    }
}

/* 相册未选中而关闭 → 恢复被暂停的实时预览 */
static void on_album_dismiss(void)
{
    ESP_LOGI(TAG, "Album dismissed — restart preview");
    _preview_start();
}

static void on_album_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Album clicked — open picker");

    if (vision_is_busy()) {
        show_toast("任务进行中，请稍候");
        return;
    }

    /* ★ 打开相册前暂停实时预览:
     *   album_scan (fopen+probe+读取, 9 张约 100ms+) 在 LVGL GUI task 内同步执行,
     *   会阻塞 flush → preview timer 无法刷新 → 恢复后帧跳跃/撕裂。
     *   先停预览 (timer+task), 扫描期间无预览需刷新, 消除阻塞窗口的可见撕裂。
     *   选中 → on_album_pick 接管; 未选中关闭 → on_album_dismiss 重启预览。 */
    _preview_stop();

    /* 打开相册选择器 (惰性挂载 + 扫描发生在此调用内部) */
    ocr_album_ui_show(on_album_pick, on_album_dismiss);
}

/* ═══════════════════════════════════════════════
   相册导入 — 选中一张照片后的处理
   ═══════════════════════════════════════════════ */

/* 在预览区显示 SD 卡上的 JPEG 文件 (走 LVGL HW 解码器 + POSIX FS 盘符 S:)
 * 必须在 LVGL 线程调用。 */
static void ocr_ui_show_jpeg_file(const char *lv_src)
{
    if (!s_preview_image || !lv_src) return;

    /* 用文件路径作为 image src → esp_lv_decoder 自动解码 JPEG */
    lv_image_set_src(s_preview_image, lv_src);
    lv_image_set_inner_align(s_preview_image, LV_IMAGE_ALIGN_CONTAIN);
    /* 标记: 已污染缩放状态, 恢复实时预览时需复位为 1:1 */
    s_preview_scale_dirty = true;

    lv_obj_clear_flag(s_preview_image, LV_OBJ_FLAG_HIDDEN);
    if (s_placeholder) {
        lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 相册回调: jpg_path 形如 "/sdcard/photos/xxx.jpg"
 * 运行在 LVGL 线程 (由弹窗点击事件触发)。 */
static void on_album_pick(const char *jpg_path)
{
    if (!jpg_path) { _preview_start(); return; }
    ESP_LOGI(TAG, "Import picked: %s", jpg_path);

    if (vision_is_busy()) {
        show_toast("任务进行中，请稍候");
        _preview_start();
        return;
    }

    /* 端点配置 (与拍照路径一致) */
    if (!s_vision_ep.api_url) {
        _ocr_endpoint_load();
    }
    if (!s_vision_ep.api_key) {
        show_toast("请先配置通义千问 API Key");
        _preview_start();
        return;
    }
    if (!s_solver_ep.api_key) {
        show_toast("请先配置豆包 API Key");
        _preview_start();
        return;
    }

    /* 探测 JPEG 合法性: 损坏/截断直接拒绝, 不喂给解码器/上传 */
    jpeg_probe_t pj;
    bool probe_ok = (photo_history_probe_jpeg(jpg_path, &pj) == ESP_OK);
    if (probe_ok && !pj.valid) {
        ESP_LOGE(TAG, "Import rejected — invalid JPEG: %s", jpg_path);
        show_toast("图片已损坏，无法识别");
        _preview_start();
        return;
    }
    /* 仅当硬件解码器可处理时才在预览区显示, 否则跳过显示避免解码报错
     * (OCR 仍可进行: 上传走原始字节, 不经本地解码) */
    bool can_display = !probe_ok || pj.hw_decodable;

    /* 读取 JPEG 到内存 */
    FILE *f = fopen(jpg_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", jpg_path);
        show_toast("无法读取图片");
        _preview_start();
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (2 * 1024 * 1024)) {  /* 上限 2MB, 防异常文件 */
        ESP_LOGE(TAG, "Bad JPEG size: %ld", sz);
        fclose(f);
        show_toast("图片过大或损坏");
        _preview_start();
        return;
    }
    uint8_t *jpeg = malloc((size_t)sz);
    if (!jpeg) {
        fclose(f);
        show_toast("内存不足");
        _preview_start();
        return;
    }
    size_t rd = fread(jpeg, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
        ESP_LOGE(TAG, "Short read: %u/%ld", (unsigned)rd, sz);
        free(jpeg);
        show_toast("读取图片失败");
        _preview_start();
        return;
    }

    /* 预览已在 on_album_click 中停止 (task+timer 均停)。直接静态显示导入图片,
     * 无需冻结。OCR 完成后由 _ocr_msg_process 在 LVGL 线程重启预览 (见 s_restart_preview_after_ocr)。 */
    s_preview_pending = false;
    s_restart_preview_after_ocr = true;

    /* /sdcard/photos/x.jpg → S:/photos/x.jpg
     * 显式限长拼接, 避免 snprintf("%s") 对无界指针触发 format-truncation。 */
    const char *mp = sd_card_get_mount_point();
    size_t mp_len = strlen(mp);
    const char *rel = (strncmp(jpg_path, mp, mp_len) == 0) ? jpg_path + mp_len : jpg_path;
    char lv_src[96];
    size_t rel_len = strlen(rel);
    if (rel_len > sizeof(lv_src) - 3) rel_len = sizeof(lv_src) - 3;
    lv_src[0] = 'S';
    lv_src[1] = ':';
    memcpy(lv_src + 2, rel, rel_len);
    lv_src[2 + rel_len] = '\0';
    if (can_display) {
        ocr_ui_show_jpeg_file(lv_src);   /* 硬件可解码 → 预览区显示导入图片 */
    } else {
        ESP_LOGW(TAG, "Skip preview display (HW-undecodable), OCR continues");
    }

    s_cancel_requested = false;

    /* 启动导入 OCR 流水线 (复用拍照的所有回调) */
    vision_callback_t cb = {
        .on_state    = _on_ocr_state,
        .on_progress = NULL,
        .on_result   = _on_ocr_result,
        .on_error    = _on_ocr_error,
    };

    int ret = vision_ocr_start_from_jpeg(jpeg, (size_t)sz,
                                         &s_vision_ep, &s_solver_ep, &cb, NULL);
    free(jpeg);  /* vision_ocr_start_from_jpeg 内部已复制, 此处释放本地副本 */

    if (ret != 0) {
        ESP_LOGE(TAG, "vision_ocr_start_from_jpeg failed: 0x%x", ret);
        s_restart_preview_after_ocr = false;
        _preview_start();   /* 启动失败, 立即恢复实时预览 */
        show_toast("启动识别失败");
    }
}

static void on_wrongbook_click(lv_event_t *e)
{
    (void)e;
    if (!s_has_current_result) {
        show_toast("请先拍照识别题目");
        return;
    }

    /* 拼接 tags 为逗号分隔字符串 */
    char tags_str[256] = {0};
    for (int i = 0; i < s_current_result.tag_count; i++) {
        if (i > 0) strncat(tags_str, ",", sizeof(tags_str) - strlen(tags_str) - 1);
        strncat(tags_str, s_current_result.tags[i],
                sizeof(tags_str) - strlen(tags_str) - 1);
    }

    /* 学科: 从 LLM 全局状态获取 */
    extern int ai_llm_get_current_subject(void);
    const char *subjects[] = {"数学", "英语", "语文", "物理", "化学"};
    int subj_idx = ai_llm_get_current_subject();
    const char *subject = (subj_idx >= 0 && subj_idx < 5) ? subjects[subj_idx] : "";

    esp_err_t ret = wrong_book_add(
        s_current_result.question,
        subject,
        tags_str,
        s_current_result.answer
    );

    if (ret == ESP_OK) {
        s_has_current_result = false;  /* 防重复标记 */
        show_toast("已加入错题本!");
    } else {
        show_toast("保存失败，请检查SD卡");
    }
}

static void on_ai_detail_click(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "AI detail clicked (feature disabled)");
    show_toast("深入讲解功能暂未开放，敬请期待！");
}

/* ── 知识点详情映射表 ── */
typedef struct {
    const char *keyword;
    const char *detail;
} tag_detail_t;

static const tag_detail_t s_tag_details[] = {
    {"完全平方公式", "a^2 + 2ab + b^2 = (a+b)^2"},
    {"一元二次方程", "ax^2 + bx + c = 0 (a!=0)"},
    {"因式分解",     "提公因式/公式法/十字相乘法"},
    {NULL, NULL}
};

static void on_tag_click(lv_event_t *e)
{
    lv_obj_t *tag = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(tag, 0);
    const char *text = lv_label_get_text(label);

    ESP_LOGI(TAG, "Tag clicked: %s", text);

    const tag_detail_t *item = s_tag_details;
    while (item->keyword != NULL) {
        if (strstr(text, item->keyword)) {
            show_toast(item->detail);
            return;
        }
        item++;
    }
    show_toast("暂无详细讲解");
}

/* 按钮按压效果: 变色 + 缩放 */
static void on_btn_pressed(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_color_t color = lv_color_hex((uint32_t)(intptr_t)lv_event_get_user_data(e));

    lv_obj_set_style_bg_color(btn, color, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, btn);
    lv_anim_set_values(&a, 256, 243);
    lv_anim_set_duration(&a, 100);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_btn_scale_cb);
    lv_anim_start(&a);
}

static void on_btn_released(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_color_t color = lv_color_hex((uint32_t)(intptr_t)lv_event_get_user_data(e));

    lv_obj_set_style_bg_color(btn, color, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, btn);
    lv_anim_set_values(&a, 243, 256);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_btn_scale_cb);
    lv_anim_start(&a);
}

/* ── 动画回调函数 (替代 C++ lambda) ── */

static void anim_btn_scale_cb(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_style_transform_scale_x(obj, v, 0);
    lv_obj_set_style_transform_scale_y(obj, v, 0);
}

static void anim_toast_opa_cb(lv_obj_t *obj, int32_t v)
{
    lv_obj_set_style_opa(obj, v, 0);
}

static void on_scan_completed(lv_anim_t *anim)
{
    (void)anim;
    lv_obj_add_flag(s_scan_line, LV_OBJ_FLAG_HIDDEN);
}

static void on_toast_fade_completed(lv_anim_t *anim)
{
    (void)anim;
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}