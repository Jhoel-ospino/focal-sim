#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline bool lvgl_port_lock(uint32_t timeout_ms) {
    (void)timeout_ms;
    return true;
}

static inline void lvgl_port_unlock(void) {}
static inline void ili9488_wait_idle(void) {}

#ifdef __cplusplus
}
#endif
