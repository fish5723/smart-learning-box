/**
 * @file icon_loader.c
 * @brief PNG 图标加载器实现 — 从 TF 卡 emoji 文件夹加载图标
 *
 * 关键设计:
 *   - 图标源文件在 TF 卡: /sdcard/emoji/<filename>.png
 *   - LVGL 9.4 通过 lv_image_set_src(img, "S:/emoji/xxx.png") 加载 PNG
 *   - 需要启用 LV_USE_LIBPNG 或 LV_USE_LODEPNG (sdkconfig)
 *   - SD 卡不可用时, lv_image 回退为空，外层代码应检查并改用 lv_label + LV_SYMBOL
 *
 * 注意:
 *   - LVGL 9.4 的 lv_image 支持运行时 PNG 但需要 decoder 组件
 *   - 本项目 SD 卡通过 FatFS 挂载在 /sdcard, LVGL FS 驱动器字母为 'S'
 *   - LVGL FS (POSIX, 盘符 S:) 映射到 /sdcard, 文件路径 "S:/emoji/xxx.png"
 *   - 需在 sdkconfig 中开启 CONFIG_LV_USE_FS_POSIX 并设 LETTER=83('S')、PATH="/sdcard"
 */

#include "icon_loader.h"
#include "bsp/storage/sd_card.h"
#include "esp_log.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "ICON_LOADER";

/* ═══════════════════════════════════════════════
   图标注册表 — 与 TF 卡 emoji 文件夹文件名对应
   ═══════════════════════════════════════════════ */
static const icon_info_t s_icon_registry[] = {
    /* P0 — 核心功能图标 */
    [ICON_ROBOT]          = { ICON_ROBOT,          "机器人-128x128.png",               LV_SYMBOL_CHARGE,    "AI" },
    [ICON_FLASH_CAMERA]   = { ICON_FLASH_CAMERA,   "闪光相机-128x128.png",             LV_SYMBOL_IMAGE,     "OCR" },
    [ICON_GAME_HANDLE]    = { ICON_GAME_HANDLE,    "游戏手柄-128x128.png",             LV_SYMBOL_PLAY,      "GAME" },
    [ICON_TROPHY]         = { ICON_TROPHY,         "trophy-128x128.png",               LV_SYMBOL_OK,        "ACHV" },
    [ICON_LEFT_ARROW]     = { ICON_LEFT_ARROW,     "左箭头-128x128.png",               LV_SYMBOL_LEFT,      "<-" },
    [ICON_SIGNAL]         = { ICON_SIGNAL,         "信号格-128x128.png",               LV_SYMBOL_WIFI,      "WiFi" },
    [ICON_BATTERY]        = { ICON_BATTERY,        "电池-128x128.png",                 LV_SYMBOL_BATTERY_FULL, "BAT" },
    [ICON_HOUSE]          = { ICON_HOUSE,          "house-128x128.png",         LV_SYMBOL_HOME,      "HOME" },

    /* P1 — 装饰/功能图标 */
    [ICON_BOOKS]          = { ICON_BOOKS,          "books-128x128.png",                LV_SYMBOL_LIST,      "BOOK" },
    [ICON_BULLSEYE]       = { ICON_BULLSEYE,       "bullseye-128x128.png",             LV_SYMBOL_GPS,       "TGT" },
    [ICON_CROWN]          = { ICON_CROWN,          "crown-128x128.png",                LV_SYMBOL_KEYBOARD,  "LV" },
    [ICON_FIRE]           = { ICON_FIRE,           "fire-128x128.png",                 LV_SYMBOL_CHARGE,    "HOT" },
    [ICON_BICEPS]         = { ICON_BICEPS,         "flexed-biceps-128x128.png",        LV_SYMBOL_POWER,     "PWR" },
    [ICON_GEM]            = { ICON_GEM,            "gem-stone-128x128.png",            LV_SYMBOL_CHARGE,      "GEM" },
    [ICON_GLOWING_STAR]   = { ICON_GLOWING_STAR,   "glowing-star-128x128.png",         LV_SYMBOL_CHARGE,      "★" },
    [ICON_HUNDRED_POINTS] = { ICON_HUNDRED_POINTS, "hundred-points-128x128.png",       LV_SYMBOL_OK,        "100" },
    [ICON_LIGHT_BULB]     = { ICON_LIGHT_BULB,     "light-bulb-128x128.png",           LV_SYMBOL_CHARGE,      "TIP" },
    [ICON_CRYING_FACE]    = { ICON_CRYING_FACE,    "loudly-crying-face-128x128.png",   LV_SYMBOL_CLOSE,     ":(" },
    [ICON_PARTY_POPPER]   = { ICON_PARTY_POPPER,   "party-popper-128x128.png",         LV_SYMBOL_BELL,      "YAY" },
    [ICON_PENCIL]         = { ICON_PENCIL,         "pencil-128x128.png",               LV_SYMBOL_EDIT,      "✎" },
    [ICON_RED_HEART]      = { ICON_RED_HEART,      "red-heart-128x128.png",            LV_SYMBOL_CHARGE,    "♥" },
    [ICON_ROCKET]         = { ICON_ROCKET,         "rocket-128x128.png",               LV_SYMBOL_UPLOAD,    "ROCK" },
    [ICON_SKULL]          = { ICON_SKULL,          "skull-128x128.png",                LV_SYMBOL_WARNING,   "ERR" },
    [ICON_SMILE]          = { ICON_SMILE,          "smiling-face-with-smiling-eyes-128x128.png", LV_SYMBOL_OK, "=)" },
    [ICON_SUNGLASSES]     = { ICON_SUNGLASSES,     "smiling-face-with-sunglasses-128x128.png",   LV_SYMBOL_OK, "COOL" },
    [ICON_STAR]           = { ICON_STAR,           "star-128x128.png",                 LV_SYMBOL_CHARGE,      "☆" },
    [ICON_THINKING_FACE]  = { ICON_THINKING_FACE,  "thinking-face-128x128.png",        LV_SYMBOL_REFRESH,   "..." },
    [ICON_THUMBS_UP]      = { ICON_THUMBS_UP,      "thumbs-up-128x128.png",            LV_SYMBOL_OK,        "OK" },
    [ICON_GIFT]           = { ICON_GIFT,           "wrapped-gift-128x128.png",         LV_SYMBOL_BELL,      "GIFT" },
    [ICON_LOCK]           = { ICON_LOCK,           "locked-128x128.png",                 LV_SYMBOL_CLOSE,     "🔒" },
    [ICON_SEND]           = { ICON_SEND,           "outbox-tray-128x128.png",             LV_SYMBOL_UPLOAD,    "SEND" },
    [ICON_UPPERCASE]      = { ICON_UPPERCASE,      "input-latin-letters-128x128.png",         LV_SYMBOL_EDIT,      "ABC" },
    [ICON_ENVELOPE_ARROW] = { ICON_ENVELOPE_ARROW, "envelope-with-arrow-128x128.png",           LV_SYMBOL_BELL,      "MAIL" },
    [ICON_DIGITS]         = { ICON_DIGITS,         "input-numbers-128x128.png",                 LV_SYMBOL_PLUS,      "123" },
};

/* ═══════════════════════════════════════════════
   状态
   ═══════════════════════════════════════════════ */
static bool s_available = false;
static char s_icon_base_path[64] = "S:/emoji/";

/* ═══════════════════════════════════════════════
   公开接口
   ═══════════════════════════════════════════════ */

esp_err_t icon_loader_init(void)
{
    ESP_LOGI(TAG, "icon_loader_init()");

    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted — icons disabled, using LV_SYMBOL fallback");
        s_available = false;
        return ESP_OK;  /* 非致命错误 */
    }

    /* 检查 emoji 目录是否存在 */
    const char *sd_root = sd_card_get_mount_point();
    char emoji_dir[64];
    snprintf(emoji_dir, sizeof(emoji_dir), "%s/emoji", sd_root);

    struct stat st;
    if (stat(emoji_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        ESP_LOGW(TAG, "emoji directory not found on SD card — icons disabled");
        s_available = false;
        return ESP_OK;
    }

    /* 验证至少一个核心图标文件存在 (机器人.png 作为 canary) */
    char test_path[128];
    snprintf(test_path, sizeof(test_path), "%s/机器人-128x128.png", emoji_dir);
    if (stat(test_path, &st) != 0) {
        ESP_LOGW(TAG, "Icon files not found (checked: %s) — icons disabled", test_path);
        s_available = false;
        return ESP_OK;
    }

    s_available = true;
    ESP_LOGI(TAG, "icon_loader_init() OK — %d icons available from TF card",
             (int)(sizeof(s_icon_registry) / sizeof(s_icon_registry[0])));
    return ESP_OK;
}

void icon_loader_deinit(void)
{
    ESP_LOGI(TAG, "icon_loader_deinit()");
    s_available = false;
}

bool icon_loader_is_available(void)
{
    return s_available;
}

const icon_info_t *icon_loader_get_info(icon_id_t id)
{
    if (id >= ICON_COUNT) return NULL;
    return &s_icon_registry[id];
}

const char *icon_loader_get_path(icon_id_t id)
{
    if (!s_available) return NULL;
    if (id >= ICON_COUNT) return NULL;

    /* 返回 LVGL 文件系统路径: "S:/emoji/<filename>" */
    static char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s%s",
             s_icon_base_path, s_icon_registry[id].filename);
    return full_path;
}

void icon_loader_set_image(lv_obj_t *img, icon_id_t id, int size)
{
    if (!img) return;

    if (s_available) {
        const char *path = icon_loader_get_path(id);
        if (path) {
            lv_image_set_src(img, path);
            /* 缩放: 原图 128×128 → 目标尺寸 */
            if (size > 0 && size < 128) {
                lv_image_set_scale(img, (size * 256) / 128);  /* LVGL scale: 256 = 1.0x */
            } else if (size > 128) {
                lv_image_set_scale(img, (size * 256) / 128);
            } else {
                lv_image_set_scale(img, 256);  /* 原始 128×128 */
            }
            return;
        }
    }

    /* 回退: 无法显示 PNG, 保留 lv_image 空状态 */
    ESP_LOGV(TAG, "Icon %d not available (SD=%d), image left blank", id, s_available);
}

lv_obj_t *icon_loader_create_image(lv_obj_t *parent, icon_id_t id,
                                    int width, int height)
{
    if (height <= 0) height = width;
    const icon_info_t *info = icon_loader_get_info(id);

    if (s_available && info) {
        /* PNG 图标 — 使用 lv_image */
        lv_obj_t *img = lv_image_create(parent);
        lv_obj_set_size(img, width, height);

        const char *path = icon_loader_get_path(id);
        if (path) {
            lv_image_set_src(img, path);
            /* 计算缩放比例 (原图 128×128) */
            int scale = (width * 256) / 128;
            if (scale > (height * 256) / 128) {
                scale = (height * 256) / 128;
            }
            lv_image_set_scale(img, scale > 0 ? scale : 256);
        }
        return img;
    }

    /* 回退: 使用 lv_label + LV_SYMBOL */
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_size(label, width, height);

    if (info && info->lv_symbol) {
        lv_label_set_text(label, info->lv_symbol);
    } else if (info && info->fallback_text) {
        lv_label_set_text(label, info->fallback_text);
    } else {
        lv_label_set_text(label, "?");
    }
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    return label;
}
