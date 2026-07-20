/**
 * @file nofrendo_stubs.c — stub implementations for removed subsystems
 *
 * These are MINIMAL stubs for compilation. Real implementations go into
 * main/app/nes_emu/platform/ during Phase 4 (display + input + ROM loading).
 */
#include "nofrendo_stubs.h"
#include <string.h>

/* ── Timer ticks ── */
volatile int nofrendo_ticks = 0;

/* ── Config (never used by core, but nofconfig.h declares extern) ── */
static bool _config_open(void) { return true; }
static void _config_close(void) {}
static int  _config_read_int(const char *g, const char *k, int def) { (void)g; (void)k; return def; }
static const char *_config_read_string(const char *g, const char *k, const char *def) { (void)g; (void)k; return def; }
static void _config_write_int(const char *g, const char *k, int v) { (void)g; (void)k; (void)v; }
static void _config_write_string(const char *g, const char *k, const char *v) { (void)g; (void)k; (void)v; }

config_t config = {
    .open        = _config_open,
    .close       = _config_close,
    .read_int    = _config_read_int,
    .read_string = _config_read_string,
    .write_int   = _config_write_int,
    .write_string = _config_write_string,
    .filename    = "na",
};

/* ── OSD stubs ── */

/* osd_getromdata() 定义在 platform/nes_core.c — 仅在 ROM 加载后有效 */

void osd_getsoundinfo(sndinfo_t *info)
{
    if (info) {
        info->sample_rate = 22100;
        info->bps = 16;
    }
}

void osd_setsound(void (*playfunc)(void *buffer, int size))
{
    (void)playfunc;  /* Audio disabled (Phase 4) */
}

void osd_getinput(void)
{
    /* Input disabled (Phase 4) */
}

void osd_fullname(char *fullname, const char *shortname)
{
    if (fullname && shortname)
        strncpy(fullname, shortname, 512);
}

char *osd_newextension(char *string, char *ext)
{
    (void)ext;
    return string;  /* No extension change in stub */
}
