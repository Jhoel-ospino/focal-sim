/**
 * @file liquid_wave_progress.h
 * @brief Dynamic 3-Layer Liquid Wave Progress Component for Focal CNC.
 *
 * Implements a liquid wave progress indicator applied directly over a 2D G-code silhouette:
 * 1. Base Layer (Background): Dim silhouette at 20% opacity (passive visual guide).
 * 2. Content Layer (Revealed): Vibrant silhouette at 100% opacity in Pastel Mint.
 * 3. Dynamic Wave Mask: Oscillating sinusoidal wave boundary moving from 0% (bottom)
 *    to 100% (top) in lockstep with cut progress, progressively revealing the active cut.
 *
 * Designed for ESP32-S3 (8MB PSRAM) and LVGL v8 with TrueColor RGB565 rendering.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct liquid_wave_progress liquid_wave_progress_t;

/**
 * @brief Creates a liquid wave progress widget.
 * @param parent Parent LVGL container
 * @param width Canvas width in pixels (e.g. 176)
 * @param height Canvas height in pixels (e.g. 180)
 * @return Opaque pointer to the widget, or NULL on failure.
 */
liquid_wave_progress_t *liquid_wave_progress_create(lv_obj_t *parent, int16_t width, int16_t height);

/**
 * @brief Gets the root container LVGL object of the widget for layout positioning.
 */
lv_obj_t *liquid_wave_progress_get_obj(liquid_wave_progress_t *wp);

/**
 * @brief Loads and pre-renders a G-code file into both base (20% dim) and active (100% vibrant) buffers.
 * @param wp Widget handle
 * @param filepath Path to the .gcode file
 */
void liquid_wave_progress_load_file(liquid_wave_progress_t *wp, const char *filepath);

/**
 * @brief Updates the liquid progress value.
 * @param wp Widget handle
 * @param progress Normalized progress between 0.0f (empty/bottom) and 1.0f (full/top).
 */
void liquid_wave_progress_set_progress(liquid_wave_progress_t *wp, float progress);

/**
 * @brief Sets whether the wave animation continues oscillating.
 */
void liquid_wave_progress_set_animating(liquid_wave_progress_t *wp, bool animating);

/**
 * @brief Updates theme palette (dark mode vs light mode).
 */
void liquid_wave_progress_set_theme(liquid_wave_progress_t *wp, bool is_dark);

/**
 * @brief Destroys the widget, stops its animation timer, and frees PSRAM buffers.
 */
void liquid_wave_progress_destroy(liquid_wave_progress_t *wp);

#ifdef __cplusplus
}
#endif

