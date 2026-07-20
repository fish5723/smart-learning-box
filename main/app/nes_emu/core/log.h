/**
 * @file log.h — stub replacement for nofrendo log system
 *
 * Original: components/nofrendo/log.h — console/file logging
 * Replaced: all log_printf() → ESP_LOGI("N_CORE", ...)
 */
#pragma once
#include "esp_log.h"
#define log_printf(...)   ESP_LOGI("N_CORE", __VA_ARGS__)
#define log_init()        0
#define log_shutdown()    ((void)0)
