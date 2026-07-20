/**
 * @file nes_core.c
 * @brief NES 模拟核心集成层
 *
 * 显示管线:
 *   PPU (8-bit indexed pixels, 256×240)
 *     → bitmap_t (vid_getbuffer, 61KB DRAM)
 *       → pal_convert to RGB565 (120KB PSRAM)
 *         → nes_render_update() → LVGL canvas → MIPI DSI
 *
 * ROM 数据:
 *   从 SD 卡 fopen/fread → PSRAM (heap_caps_malloc, SPIRAM)
 *   osd_getromdata() 返回此指针供 rom_load() 使用
 *
 * 核心调用 (Core 0, NES task):
 *   nes_emulate_frame() → 262 扫描线渲染
 *   转换 → signal (frame_sem)
 *
 * LVGL 读取 (Core 1):
 *   refresh_frame_cb → nes_core_get_framebuffer() → nes_render_update()
 */

#include "nes_core.h"
#include "nofrendo_stubs.h"
#include "nes.h"
#include "vid_drv.h"
#include "nes_ppu.h"
#include "nes_apu.h"
#include "nes_rom.h"
#include "nes_pal.h"
#include "nes6502.h"
#include "gui.h"
#include "noftypes.h"
#include "bitmap.h"
#include "bsp/storage/sd_card.h"
#include "esp_log.h"
#include <string.h>
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include<stdbool.h>
static const char *TAG = "NES_CORE";

/* ═══════════════════════════════════════════════
   标准 NES 调色板 (64色 × RGB888, 避免运行时 sin/cos)
   ═══════════════════════════════════════════════ */
static const rgb_t NES_DEFAULT_PAL[64] = {
    {124,124,124},{0,0,252},{0,0,188},{68,40,188},
    {148,0,132},{168,0,32},{168,16,0},{136,20,0},
    {80,48,0},{0,120,0},{0,104,0},{0,88,0},
    {0,64,88},{0,0,0},{0,0,0},{0,0,0},
    {188,188,188},{0,120,248},{0,88,248},{104,68,252},
    {216,0,204},{228,0,88},{216,40,0},{200,68,0},
    {136,112,0},{0,184,0},{0,168,0},{0,144,0},
    {0,120,136},{0,0,0},{0,0,0},{0,0,0},
    {252,252,252},{60,188,252},{104,136,252},{152,120,252},
    {252,120,252},{252,88,188},{252,132,84},{252,160,68},
    {196,188,0},{80,248,60},{0,236,0},{60,208,0},
    {0,200,180},{0,0,0},{0,0,0},{0,0,0},
    {252,252,252},{188,236,252},{208,208,252},{216,192,252},
    {252,192,252},{252,188,220},{252,212,172},{252,224,148},
    {236,236,0},{168,252,136},{184,248,128},{164,236,108},
    {0,220,216},{0,0,0},{0,0,0},{0,0,0},
};

/* ═══════════════════════════════════════════════
   内部状态
   ═══════════════════════════════════════════════ */
static bool       s_initialized = false;
static bool       s_rom_loaded  = false;
static nes_t     *s_nes         = NULL;  /* NES 核心实例 */
static uint8_t   *s_rom_data    = NULL;  /* ROM 文件数据 (PSRAM) */

/* 帧缓冲 */
static uint16_t  *s_rgb565     = NULL;   /* RGB565 输出 (256×240, PSRAM) */
static uint16_t   s_pal_lut[64];         /* NES 调色板 → RGB565 查找表 */

/* ── 输入系统 (nesinput_t 注册到 Nofrendo 核心) ── */
#include "nesinput.h"
static nesinput_t s_player0_input = {
    .type = INP_JOYPAD0,
    .data = 0,
};

/* ── OSD ROM data 接口 (被 nes_rom.c rom_load() 调用) ── */
static uint8_t *s_osd_rom_ptr = NULL;

void _set_osd_rom_data(uint8_t *data) { s_osd_rom_ptr = data; }
unsigned char *osd_getromdata(void)   { return (unsigned char *)s_osd_rom_ptr; }

/* ── 输入设置接口 (被 nes_emu_input 调用) ── */
void nes_core_set_input(uint8_t key_mask, bool pressed)
{
    input_event(&s_player0_input,
                pressed ? INP_STATE_MAKE : INP_STATE_BREAK,
                key_mask);
}

/* ═══════════════════════════════════════════════
   RGB565 调色板
   ═══════════════════════════════════════════════ */

static void build_palette_lut(void)
{
    nes_t *ctx = nes_getcontextptr();
    if (!ctx || !ctx->ppu) {
        /* 回退: 使用标准 NES 默认调色板 */
        for (int i = 0; i < 64; i++) {
            rgb_t c = nes_palette[i];
            s_pal_lut[i] = (uint16_t)(((c.r >> 3) << 11) |
                                       ((c.g >> 2) << 5)  |
                                        (c.b >> 3));
        }
        return;
    }
    for (int i = 0; i < 64; i++) {
        rgb_t c = ctx->ppu->curpal[i];
        s_pal_lut[i] = (uint16_t)(((c.r >> 3) << 11) |
                                   ((c.g >> 2) << 5)  |
                                    (c.b >> 3));
    }
}

/* ═══════════════════════════════════════════════
   自定义 NES 机器创建 (避免 apu_create/ppu_create 的 double 运算)
   ═══════════════════════════════════════════════ */
static nes_t *nes_core_create_machine(void)
{
    nes_t *machine = malloc(sizeof(nes_t));
    if (!machine) return NULL;
    memset(machine, 0, sizeof(nes_t));

    /* CPU */
    machine->cpu = malloc(sizeof(nes6502_context));
    if (!machine->cpu) { free(machine); return NULL; }
    memset(machine->cpu, 0, sizeof(nes6502_context));

    machine->cpu->mem_page[0] = malloc(0x800); /* 2KB NES RAM */
    if (!machine->cpu->mem_page[0]) {
        free(machine->cpu); free(machine); return NULL;
    }
    for (int i = 1; i < NES6502_NUMBANKS; i++)
        machine->cpu->mem_page[i] = NULL;

    machine->cpu->read_handler = machine->readhandler;
    machine->cpu->write_handler = machine->writehandler;
    machine->autoframeskip = true;

    /* PPU — zeroed, then fill palette from static data (避免 vid_setpalette) */
    machine->ppu = malloc(sizeof(ppu_t));
    if (!machine->ppu) {
        free(machine->cpu->mem_page[0]); free(machine->cpu); free(machine);
        return NULL;
    }
    memset(machine->ppu, 0, sizeof(ppu_t));
    machine->ppu->drawsprites = true;
    for (int i = 0; i < 64; i++)
        machine->ppu->curpal[i] = NES_DEFAULT_PAL[i];
    /* 副本 (3x for priority) */
    for (int i = 0; i < 3; i++)
        memcpy(&machine->ppu->curpal[64 + i * 64], machine->ppu->curpal, 64 * sizeof(rgb_t));
    /* GUI 色 */
    for (int i = 0; i < 11; i++)
        machine->ppu->curpal[192 + i] = gui_pal[i];

    /* APU — zeroed struct, 无声 (Phase 4 不需音频) */
    machine->apu = calloc(1, sizeof(apu_t));
    if (!machine->apu) {
        free(machine->ppu); free(machine->cpu->mem_page[0]);
        free(machine->cpu); free(machine);
        return NULL;
    }

    machine->poweroff = false;
    machine->pause = false;
    return machine;
}

/* ═══════════════════════════════════════════════
   公开 API
   ═══════════════════════════════════════════════ */

esp_err_t nes_core_init(void)
{
    if (s_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Initializing NES core");

    /* 1/4: 分配 RGB565 输出缓冲 (120 KB PSRAM) */
    ESP_LOGI(TAG, "  step 1/4: alloc RGB565 buffer");
    s_rgb565 = heap_caps_malloc(256 * 240 * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_rgb565) {
        ESP_LOGE(TAG, "PSRAM alloc failed for RGB565 buffer");
        return ESP_ERR_NO_MEM;
    }
    memset(s_rgb565, 0, 256 * 240 * sizeof(uint16_t));

    /* 2/4: 创建视频缓冲 (vid_drv: primary_buffer) */
    ESP_LOGI(TAG, "  step 2/4: vid_setmode");
    if (vid_setmode(256, 240)) {
        ESP_LOGE(TAG, "vid_setmode(256, 240) failed");
        heap_caps_free(s_rgb565);
        s_rgb565 = NULL;
        return ESP_FAIL;
    }

    /* 3/4: 创建 NES 核心实例 (自定义创建, 避免 double 运算) */
    ESP_LOGI(TAG, "  step 3/4: nes_core_create_machine");
    s_nes = nes_core_create_machine();
    if (!s_nes) {
        ESP_LOGE(TAG, "nes_core_create_machine() failed");
        heap_caps_free(s_rgb565);
        s_rgb565 = NULL;
        return ESP_FAIL;
    }

    /* 4/4: 构建默认调色板 LUT + 注册输入设备 */
    ESP_LOGI(TAG, "  step 4/4: palette LUT + input register");
    for (int i = 0; i < 64; i++) {
        uint8_t r = 0, g = 0, b = 0;
        switch ((i >> 3) & 7) {
        case 0: r = 255; g = (i & 7) * 36; break;
        case 1: r = 255 - (i & 7) * 36; g = 255; break;
        case 2: g = 255; b = (i & 7) * 36; break;
        case 3: g = 255 - (i & 7) * 36; b = 255; break;
        case 4: b = 255; r = (i & 7) * 36; break;
        case 5: r = 255; b = 255 - (i & 7) * 36; break;
        case 6: r = g = b = 255 - (i & 7) * 32; break;
        default: break;
        }
        s_pal_lut[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    /* 注册玩家1输入设备到Nofrendo核心 */
    input_register(&s_player0_input);
    ESP_LOGI(TAG, "  Player 0 input registered");

    s_initialized = true;
    ESP_LOGI(TAG, "NES core ready");
    return ESP_OK;
}

esp_err_t nes_core_load_rom(const char *path)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Core not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!path || !*path) {
        ESP_LOGE(TAG, "Invalid ROM path");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading ROM: %s", path);

    /* ── 打开 ROM 文件 ── */
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed", path);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0 || fsize > 1024 * 1024) {  /* Max 1MB ROM */
        ESP_LOGE(TAG, "Invalid ROM size: %ld", fsize);
        fclose(f);
        return ESP_FAIL;
    }
    fseek(f, 0, SEEK_SET);

    /* ── 读取 ROM 到 PSRAM ── */
    s_rom_data = heap_caps_malloc((size_t)fsize, MALLOC_CAP_SPIRAM);
    if (!s_rom_data) {
        ESP_LOGE(TAG, "PSRAM alloc failed for ROM (%ld B)", fsize);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t rd = fread(s_rom_data, 1, (size_t)fsize, f);
    fclose(f);
    if (rd != (size_t)fsize) {
        ESP_LOGE(TAG, "fread failed: %zu of %ld", rd, fsize);
        heap_caps_free(s_rom_data);
        s_rom_data = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ROM loaded to PSRAM: %ld bytes", fsize);

    /* ── 插入 ROM 到 NES 核心 ── */
    _set_osd_rom_data(s_rom_data);

    /* 重载: 完全销毁旧机器, 创建新机器 */
    if (s_rom_loaded) {
        ESP_LOGI(TAG, "  reload: destroy old machine + create new");

        /* 防止 rom_free 释放 PSRAM 内部指针 */
        if (s_nes->rominfo) {
            s_nes->rominfo->rom = NULL;
            s_nes->rominfo->vrom = NULL;
        }

        /* 销毁旧机器 (释放 malloc 的 rominfo/sram/vram/mmc/cpu/ppu/apu) */
        nes_destroy(&s_nes);
        s_nes = NULL;

        /* 重建视频缓冲 (旧缓冲已随机器销毁) */
        vid_setmode(256, 240);

        /* 创建全新机器 */
        s_nes = nes_core_create_machine();
        if (!s_nes) {
            ESP_LOGE(TAG, "nes_core_create_machine() failed on reload");
            heap_caps_free(s_rom_data);
            s_rom_data = NULL;
            return ESP_FAIL;
        }
    }

    if (nes_insertcart(path, s_nes)) {
        ESP_LOGE(TAG, "nes_insertcart() failed");
        heap_caps_free(s_rom_data);
        s_rom_data = NULL;
        return ESP_FAIL;
    }

    /* 重建调色板 */
    build_palette_lut();

    s_rom_loaded = true;
    ESP_LOGI(TAG, "ROM loaded successfully");
    return ESP_OK;
}

void nes_core_run_frame(void)
{
    if (!s_initialized || !s_rom_loaded) return;

    /* 重置 tick 计数器 (nes_emulate_frame 内部用) */
    nofrendo_ticks = 0;

    /* 渲染一帧: 262 扫描线, CPU + PPU 执行 */
    nes_emulate_frame();

    /* 将 8-bit indexed → RGB565 (visible area: 240 lines) */
    bitmap_t *bmp = vid_getbuffer();
    if (!bmp || !bmp->data) {
        /* 第一次运行: 打印诊断信息 */
        static int diag_count = 0;
        if (diag_count++ == 0)
            ESP_LOGW(TAG, "  vid_getbuffer returned NULL or empty data");
        return;
    }

    for (int y = 0; y < 240; y++) {
        uint8_t *src = bmp->line[y];
        uint16_t *dst = &s_rgb565[y * 256];
        for (int x = 0; x < 256; x++) {
            dst[x] = s_pal_lut[src[x] & 0x3F];
        }
    }
}

uint16_t *nes_core_get_framebuffer(void)
{
    return s_rgb565;
}

void nes_core_destroy(void)
{
    if (!s_initialized) return;

    if (s_nes) {
        nes_poweroff();
        nes_emulate_frame();

        /* 防止 rom_free() 释放 PSRAM 内部指针 (s_rom_data 统一管理) */
        if (s_nes->rominfo) {
            s_nes->rominfo->rom = NULL;
            s_nes->rominfo->vrom = NULL;
        }
        nes_destroy(&s_nes);
        s_nes = NULL;
    }

    if (s_rom_data) {
        heap_caps_free(s_rom_data);
        s_rom_data = NULL;
    }
    if (s_rgb565) {
        heap_caps_free(s_rgb565);
        s_rgb565 = NULL;
    }

    s_initialized = false;
    s_rom_loaded = false;

    ESP_LOGI(TAG, "NES core destroyed");
}