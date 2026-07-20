/**
 * @file memguard.h — Nofrendo memory guard (DISABLED for ESP32-P4)
 *
 * Original: wraps malloc/free/strdup with debug tracking.
 * Removed: conflicts with ESP-IDF standard library (malloc/free macros break newlib).
 * All core allocations go through heap_caps_malloc(..., MALLOC_CAP_SPIRAM).
 */
#pragma once
/* memguard disabled — standard malloc/free/strdup from libc */
