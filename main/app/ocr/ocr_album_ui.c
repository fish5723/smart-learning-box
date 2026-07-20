/**
 * @file ocr_album_ui.c
 * @brief 相册导入 UI — 从 /sdcard/photos 选择一张 JPEG
 *
 * 设计要点:
 *   - 惰性扫描: 初始化时【不】碰 SD 卡 (SD/C6 SDIO 曾有启动冲突),
 *     仅在用户点击"导入图片"打开弹窗时才 mount(如需) + 扫描目录。
 *   - 缩略图直接用 LVGL 硬件 JPEG 解码器 + POSIX FS (盘符 S: → /sdcard) 渲染。
 *   - 点击某张 → 关闭弹窗 → 回调 on_pick(完整 POSIX 路径)。
 */

#include "ocr_album_ui.h"
#include "app/font_loader/font_loader.h"
#include "app/photos/photo_history.h"
#include "bsp/storage/sd_card.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include <dirent.h>

static const char *TAG = "ALBUM_UI";

/* ═══════════════════════════════════════════════
   Colors
   ═══════════════════════════════════════════════ */
#define COLOR_CARD            lv_color_hex(0x1E293B)
#define COLOR_BORDER          lv_color_hex(0x334155)
#define COLOR_BG              lv_color_hex(0x0F172A)
#define COLOR_TEXT_PRIMARY    lv_color_hex(0xFFFFFF)
#define COLOR_TEXT_SECONDARY  lv_color_hex(0x94A3B8)
#define COLOR_TITLE_BLUE      lv_color_hex(0x60A5FA)

#define SPACING_SM  8
#define SPACING_MD  12
#define SPACING_LG  16

#define DIALOG_W    720
#define DIALOG_H    520
#define THUMB_W     150
#define THUMB_H     130

/* 一次最多列出的照片数 (限制软件 JPEG 解码/内存压力: P4 LVGL 线程中
 * 软件解码 800x640 缩略图开销大, 限制并发数量) */
#define ALBUM_MAX_ITEMS  12
#define PHOTOS_DIR       "photos"

/* ═══════════════════════════════════════════════
   State
   ═══════════════════════════════════════════════ */
static lv_obj_t            *s_dialog     = NULL;
static ocr_album_pick_cb_t  s_on_pick    = NULL;
static ocr_album_dismiss_cb_t s_on_dismiss = NULL;

/* 选中路径需在事件回调期间保持有效 → 静态存储, 事件 user_data 指向此处 */
static char s_pick_paths[ALBUM_MAX_ITEMS][80];  /* "/sdcard/photos/xxx.jpg" */
static int  s_item_count = 0;

/* ═══════════════════════════════════════════════
   Helpers
   ═══════════════════════════════════════════════ */

static void album_close(void)
{
    if (s_dialog) {
        lv_obj_delete(s_dialog);
        s_dialog = NULL;
    }
}

static void on_close_click(lv_event_t *e)
{
    (void)e;
    ocr_album_dismiss_cb_t cb = s_on_dismiss;
    album_close();
    if (cb) cb();   /* 未选中而关闭 → 通知调用方恢复预览 */
}

static void on_thumb_click(lv_event_t *e)
{
    const char *path = (const char *)lv_event_get_user_data(e);
    if (!path) return;

    ESP_LOGI(TAG, "Picked: %s", path);

    ocr_album_pick_cb_t cb = s_on_pick;
    s_on_dismiss = NULL;           /* 选中: 不触发 dismiss (预览由 on_pick 流程接管) */
    album_close();                 /* 先关弹窗 */
    if (cb) cb(path);              /* 再回调 (可能启动 OCR) */
}

/* 判断文件名是否 .jpg / .jpeg (大小写不敏感) */
static bool is_jpeg_name(const char *name)
{
    size_t n = strlen(name);
    if (n < 5) return false;
    const char *ext = name + n;
    /* .jpg */
    if (n >= 4 && strcasecmp(ext - 4, ".jpg") == 0)  return true;
    /* .jpeg */
    if (n >= 5 && strcasecmp(ext - 5, ".jpeg") == 0) return true;
    return false;
}

/* 扫描 /sdcard/photos 下的 JPEG → s_pick_paths[], 返回数量 */
static int album_scan(void)
{
    s_item_count = 0;

    /* dir_path 仅由挂载点("/sdcard") + "/photos" 组成, 长度可控 */
    char dir_path[48];
    int dp = snprintf(dir_path, sizeof(dir_path), "%s/" PHOTOS_DIR,
                      sd_card_get_mount_point());
    if (dp <= 0 || dp >= (int)sizeof(dir_path)) {
        ESP_LOGE(TAG, "photos dir path too long");
        return 0;
    }
    size_t dir_len = (size_t)dp;

    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "opendir(%s) failed", dir_path);
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_item_count < ALBUM_MAX_ITEMS) {
        if (!is_jpeg_name(entry->d_name)) continue;

        /* 手动拼接 dir_path + "/" + name, 显式限长; 过长文件名直接跳过。
         * 避免 snprintf("%s/%s") 对 dirent.d_name(声明为大数组) 触发
         * -Werror=format-truncation。 */
        size_t name_len = strlen(entry->d_name);
        size_t need = dir_len + 1 + name_len + 1;   /* '/' + name + '\0' */
        if (need > sizeof(s_pick_paths[0])) {
            ESP_LOGW(TAG, "skip (path too long): %s", entry->d_name);
            continue;
        }
        char *dst = s_pick_paths[s_item_count];
        memcpy(dst, dir_path, dir_len);
        dst[dir_len] = '/';
        memcpy(dst + dir_len + 1, entry->d_name, name_len);
        dst[dir_len + 1 + name_len] = '\0';

        /* 探测 JPEG: 过滤损坏/截断/尺寸不适配硬件解码器的图片,
         * 避免缩略图触发 "data units mismatch" 解码错误 */
        jpeg_probe_t pj;
        if (photo_history_probe_jpeg(dst, &pj) == ESP_OK) {
            if (!pj.valid) {
                ESP_LOGW(TAG, "skip (invalid JPEG): %s", entry->d_name);
                continue;
            }
            if (!pj.hw_decodable) {
                ESP_LOGW(TAG, "skip (HW-undecodable %dx%d): %s",
                         pj.width, pj.height, entry->d_name);
                continue;
            }
        }
        s_item_count++;
    }
    closedir(dir);

    ESP_LOGI(TAG, "Scanned %d photo(s) in %s", s_item_count, dir_path);
    return s_item_count;
}

/* 从完整 POSIX 路径构建 LVGL 盘符路径 + 显示名
 *   "/sdcard/photos/x.jpg" → src "S:/photos/x.jpg", name "x.jpg"
 * 用显式 memcpy 而非 snprintf("%s") — 后者对无界指针会触发
 * -Werror=format-truncation。 */
static void build_lv_src(const char *posix_path, char *src, size_t src_sz,
                         const char **out_name)
{
    const char *mp = sd_card_get_mount_point();       /* "/sdcard" */
    size_t mp_len = strlen(mp);
    const char *rel = posix_path;
    if (strncmp(posix_path, mp, mp_len) == 0) {
        rel = posix_path + mp_len;                     /* "/photos/x.jpg" */
    }

    /* 手动拼接 "S:" + rel, 显式限长 */
    if (src_sz >= 3) {
        size_t rel_len = strlen(rel);
        size_t max_rel = src_sz - 3;                   /* 预留 "S:" 与 '\0' */
        if (rel_len > max_rel) rel_len = max_rel;
        src[0] = 'S';
        src[1] = ':';
        memcpy(src + 2, rel, rel_len);
        src[2 + rel_len] = '\0';                        /* "S:/photos/x.jpg" */
    } else if (src_sz > 0) {
        src[0] = '\0';
    }

    const char *slash = strrchr(posix_path, '/');
    *out_name = slash ? slash + 1 : posix_path;
}

/* ═══════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════ */

void ocr_album_ui_init(void)
{
    /* 惰性: 不扫描 SD, 不创建 UI */
    ESP_LOGI(TAG, "ocr_album_ui_init() — deferred (no SD access)");
}

/* 构建一个简单信息弹窗 (SD 未就绪 / 无照片) */
static void album_show_message(const char *msg)
{
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;

    s_dialog = lv_obj_create(scr);
    lv_obj_set_size(s_dialog, 500, 220);
    lv_obj_set_style_bg_color(s_dialog, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_dialog, 20, 0);
    lv_obj_set_style_border_width(s_dialog, 2, 0);
    lv_obj_set_style_border_color(s_dialog, COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(s_dialog, SPACING_LG, 0);
    lv_obj_set_style_pad_gap(s_dialog, SPACING_MD, 0);
    lv_obj_set_flex_flow(s_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dialog,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(s_dialog);
    lv_obj_add_flag(s_dialog, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(s_dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(s_dialog);
    lv_label_set_text(label, msg);
    lv_obj_set_style_text_color(label, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(label, g_font_cjk_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *close_btn = lv_obj_create(s_dialog);
    lv_obj_set_size(close_btn, 100, 40);
    lv_obj_set_style_bg_color(close_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(close_btn, 10, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_btn, on_close_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cl = lv_label_create(close_btn);
    lv_label_set_text(cl, "关闭");
    lv_obj_set_style_text_color(cl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(cl, g_font_cjk_14, 0);
    lv_obj_center(cl);
}

/* 创建单张缩略图卡片 */
static void create_thumb_card(lv_obj_t *grid, int idx)
{
    char src[80];
    const char *name = NULL;
    build_lv_src(s_pick_paths[idx], src, sizeof(src), &name);

    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_size(card, THUMB_W, THUMB_H + 24);
    lv_obj_set_style_bg_color(card, COLOR_BG, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, SPACING_SM, 0);
    lv_obj_set_style_pad_gap(card, 4, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    /* user_data 指向静态路径 (弹窗关闭后仍有效, 直到下次 scan 覆盖) */
    lv_obj_add_event_cb(card, on_thumb_click, LV_EVENT_CLICKED, s_pick_paths[idx]);

    /* 缩略图 (HW JPEG 解码, 缩放至 THUMB_W 宽) */
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, src);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);
    lv_obj_set_size(img, THUMB_W - SPACING_SM * 2, THUMB_H);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);  /* 点击冒泡到 card */

    /* 文件名 (截断显示) */
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, name);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, THUMB_W - SPACING_SM * 2);
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(lbl, g_font_cjk_14, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
}

void ocr_album_ui_show(ocr_album_pick_cb_t on_pick,
                       ocr_album_dismiss_cb_t on_dismiss)
{
    s_on_pick    = on_pick;
    s_on_dismiss = on_dismiss;

    /* 已有弹窗先清理 */
    album_close();

    /* ── SD 卡就绪检查: 未挂载则尝试挂载 (仅点击时执行, 避开启动 SDIO 冲突) ── */
    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD not mounted — trying to mount on demand");
        if (sd_card_init() != ESP_OK || !sd_card_is_mounted()) {
            ESP_LOGE(TAG, "SD mount failed — cannot import");
            album_show_message("SD 卡未就绪\n无法导入图片");
            return;
        }
    }

    /* ── 扫描照片 ── */
    int n = album_scan();
    if (n == 0) {
        album_show_message("相册中还没有照片\n先拍照解题会自动保存");
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;

    /* ── Dialog ── */
    s_dialog = lv_obj_create(scr);
    lv_obj_set_size(s_dialog, DIALOG_W, DIALOG_H);
    lv_obj_set_style_bg_color(s_dialog, COLOR_CARD, 0);
    lv_obj_set_style_radius(s_dialog, 20, 0);
    lv_obj_set_style_border_width(s_dialog, 2, 0);
    lv_obj_set_style_border_color(s_dialog, COLOR_BORDER, 0);
    lv_obj_set_style_pad_all(s_dialog, SPACING_LG, 0);
    lv_obj_set_style_pad_gap(s_dialog, SPACING_MD, 0);
    lv_obj_set_flex_flow(s_dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(s_dialog);
    lv_obj_add_flag(s_dialog, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(s_dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(s_dialog, 200, 0);
    lv_obj_set_style_shadow_color(s_dialog, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_dialog, LV_OPA_50, 0);

    /* ── Header ── */
    lv_obj_t *header = lv_obj_create(s_dialog);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
        LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    char title_buf[32];
    lv_snprintf(title_buf, sizeof(title_buf), "选择照片 (%d)", n);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, COLOR_TITLE_BLUE, 0);
    lv_obj_set_style_text_font(title, g_font_cjk_20, 0);

    lv_obj_t *close_btn = lv_obj_create(header);
    lv_obj_set_size(close_btn, 80, 36);
    lv_obj_set_style_bg_color(close_btn, COLOR_BORDER, 0);
    lv_obj_set_style_radius(close_btn, 10, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_add_flag(close_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(close_btn, on_close_click, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(close_btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "关闭");
    lv_obj_set_style_text_color(close_label, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(close_label, g_font_cjk_14, 0);
    lv_obj_center(close_label);

    /* ── Thumbnail grid (scrollable, row-wrap) ── */
    lv_obj_t *grid = lv_obj_create(s_dialog);
    lv_obj_set_size(grid, LV_PCT(100), 0);
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_gap(grid, SPACING_MD, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);

    for (int i = 0; i < n; i++) {
        create_thumb_card(grid, i);
    }
}
