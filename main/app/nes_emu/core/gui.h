/**
 * @file gui.h — stub replacement for nofrendo GUI overlay system
 *
 * Original: components/nofrendo/gui.h — text overlay on game screen
 * Replaced: gui_sendmsg() → ESP_LOGI; color constants kept for compilation
 */
#pragma once
#include "esp_log.h"
#include <bitmap.h>

/* GUI color indices (original definitions, kept for compatibility) */
#define GUI_RED      0
#define GUI_GREEN    1
#define GUI_YELLOW   2
#define GUI_BLUE     3
#define GUI_PURPLE   4
#define GUI_CYAN     5
#define GUI_GRAY     6
#define GUI_WHITE    7
#define GUI_BLACK    8
#define GUI_ORANGE   9
#define GUI_DKGRAY   10

#define GUI_TOTALCOLORS 11

static const rgb_t gui_pal[GUI_TOTALCOLORS] = {
    {255, 0,   0  },   /* GUI_RED     */
    {0,   255, 0  },   /* GUI_GREEN   */
    {255, 255, 0  },   /* GUI_YELLOW  */
    {0,   0,   255},   /* GUI_BLUE    */
    {255, 0,   255},   /* GUI_PURPLE  */
    {0,   255, 255},   /* GUI_CYAN    */
    {192, 192, 192},   /* GUI_GRAY    */
    {255, 255, 255},   /* GUI_WHITE   */
    {0,   0,   0  },   /* GUI_BLACK   */
    {255, 165, 0  },   /* GUI_ORANGE  */
    {64,  64,  64 },   /* GUI_DKGRAY  */
};

/* gui_sendmsg: log with ESP_LOGI, first arg (color) is dropped */
#define gui_sendmsg(color, ...)  ESP_LOGI("N_CORE", __VA_ARGS__)

/* removed nofrendo GUI functions — stub */
static inline int  gui_init(void)       { return 0; }
static inline void gui_shutdown(void)   {}
static inline void gui_setrefresh(int r) { (void)r; }
static inline void gui_tick(int n)       { (void)n; }
static inline void gui_frame(bool draw)  { (void)draw; }