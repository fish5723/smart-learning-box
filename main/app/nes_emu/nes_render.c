/**
 * @file nes_render.c
 * @brief NES 帧缓冲渲染 — RGB565 → RGB888 LVGL canvas
 *
 * 数据流:
 *   InfoNES_FrameBuffer (RGB565, 256×240)
 *     → PSRAM canvas buffer (RGB888, 256×240×3)
 *       → lv_canvas (2x 硬件缩放, 512×480)
 *         → MIPI DSI LCD (RGB888, 1024×600)
 *
 * 锁策略:
 *   - nes_render_update() 内部持 esp_lv_adapter_lock 保护 canvas buffer 写入 + invalidate
 *   - nes_render_show_test_pattern() 内部持锁
 *   - nes_render_init() 由 UI 层在 LVGL 线程中调用, 不额外持锁
 */

#include "nes_render.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NES_RENDER";

/* ── 内部状态 ── */
static lv_obj_t   *s_canvas     = NULL;
static uint8_t    *s_buf        = NULL;   /* RGB888, NES_W × NES_H × 3 */
static bool        s_ready      = false;

/* ── 内部函数 ── */
static void rgb565_to_rgb888(const uint16_t *src, uint8_t *dst, int count);

/* ═══════════════════════════════════════════════
   公开 API
   ═══════════════════════════════════════════════ */

void nes_render_init(lv_obj_t *parent)
{
    if (s_ready) return;

    /* 分配 RGB888 canvas buffer: 256×240×3 = 184,320 B (~180 KB) → PSRAM */
    size_t buf_size = NES_RENDER_W * NES_RENDER_H * 3;
    s_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!s_buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed for canvas buffer (%zu B)", buf_size);
        return;
    }
    memset(s_buf, 0, buf_size);

    /* 创建 LVGL canvas */
    s_canvas = lv_canvas_create(parent ? parent : lv_screen_active());
    lv_canvas_set_buffer(s_canvas, s_buf, NES_RENDER_W, NES_RENDER_H,
                         LV_COLOR_FORMAT_RGB888);

    /* 2x 像素缩放: 对象尺寸 512×480 = 256×240 × 2 */
    lv_obj_set_size(s_canvas, NES_RENDER_W * 2, NES_RENDER_H * 2);
    lv_obj_set_style_bg_color(s_canvas, lv_color_hex(0x000000), 0);
    lv_obj_center(s_canvas);

    s_ready = true;
    ESP_LOGI(TAG, "Canvas ready: %dx%d RGB888 PSRAM → %dx%d display",
             NES_RENDER_W, NES_RENDER_H,
             NES_RENDER_W * 2, NES_RENDER_H * 2);
}

void nes_render_update(const uint16_t *buffer)
{
    if (!s_ready || !s_buf || !buffer) return;

    esp_lv_adapter_lock(-1);

    /* RGB565 → RGB888 转换到 canvas buffer */
    rgb565_to_rgb888(buffer, s_buf, NES_RENDER_W * NES_RENDER_H);

    /* 触发 LVGL 重绘 canvas 区域 */
    lv_obj_invalidate(s_canvas);

    esp_lv_adapter_unlock();
}

/* ── 彩条预计算 LUT (8 种标准色彩, RGB888) ── */
static const uint8_t s_bar_r[8] = {255, 255,   0,   0, 255, 255,   0,   0};
static const uint8_t s_bar_g[8] = {255, 255, 255, 255,   0,   0,   0,   0};
static const uint8_t s_bar_b[8] = {255,   0, 255,   0, 255,   0, 255,   0};

void nes_render_show_test_pattern(void)
{
    if (!s_ready || !s_buf) {
        ESP_LOGW(TAG, "render not initialized, cannot show test pattern");
        return;
    }

    esp_lv_adapter_lock(-1);

    /* 直接在 canvas RGB888 buffer 中生成 SMPTE 彩条, 零额外内存 */
    uint8_t *dst = s_buf;
    for (int y = 0; y < NES_RENDER_H; y++) {
        for (int x = 0; x < NES_RENDER_W; x++) {
            int bar = x * 8 / NES_RENDER_W;   /* 8 等分竖条 */
            *dst++ = s_bar_r[bar];
            *dst++ = s_bar_g[bar];
            *dst++ = s_bar_b[bar];
        }
    }

    /* 触发 LVGL 重绘 */
    lv_obj_invalidate(s_canvas);

    esp_lv_adapter_unlock();
}

lv_obj_t *nes_render_get_canvas(void)
{
    return s_canvas;
}

/* ═══════════════════════════════════════════════
   内部: RGB565 → RGB888 转换
   ═══════════════════════════════════════════════ */

static void rgb565_to_rgb888(const uint16_t *src, uint8_t *dst, int count)
{
    /* 逐像素展开:
       RGB565: RRRRRGGGGGGBBBBB (16-bit)
       → RRRRRRRR GGGGGGGG BBBBBBBB (24-bit)
       用 bit-shift + 高位填充 (低位移到高位, 低位补自身高位以保持精度) */
    for (int i = 0; i < count; i++) {
        uint16_t p = src[i];
        dst[0] = (uint8_t)((p >> 8) & 0xF8) | (uint8_t)((p >> 13) & 0x07);  /* R5 → R8 */
        dst[1] = (uint8_t)((p >> 3) & 0xFC) | (uint8_t)((p >> 9) & 0x03);   /* G6 → G8 */
        dst[2] = (uint8_t)((p << 3) & 0xF8) | (uint8_t)((p >> 2) & 0x07);   /* B5 → B8 */
        dst += 3;
    }
}
