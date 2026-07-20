/**
 * @file nofrendo_stubs.h — platform stubs for removed nofrendo subsystems
 *
 * Provides declarations for:
 *  - nofrendo_ticks (timer counter, used by nes_emulate)
 *  - config global (referenced by nofconfig.h)
 *  - OSD stubs (file/audio/input — to be properly implemented in Phase 4)
 *
 * All stubs are minimal — enough for compilation.
 * Real implementations go into main/app/nes_emu/platform/ later.
 */
#pragma once
#include "noftypes.h"
#include "nofconfig.h"
#include "osd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Global timer tick counter (incremented by timer ISR in original nofrendo) ── */
extern volatile int nofrendo_ticks;

/* ── OSD stubs (called by nes.c / nes_rom.c) ── */

/** Return ROM data pointer (original: esp_partition_mmap flash) */
unsigned char *osd_getromdata(void);

/** Fill sound info struct (original: I2S config) */
void osd_getsoundinfo(sndinfo_t *info);

/** Register audio callback */
void osd_setsound(void (*playfunc)(void *buffer, int size));

/** Poll user input */
void osd_getinput(void);

/** Resolve full file path */
void osd_fullname(char *fullname, const char *shortname);

/** Change file extension (for .sav files) */
char *osd_newextension(char *string, char *ext);

#ifdef __cplusplus
}
#endif
