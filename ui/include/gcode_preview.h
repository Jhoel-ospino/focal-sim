/**
 * @file gcode_preview.h
 * @brief High-performance, non-blocking 2D G-code silhouette preview renderer.
 *
 * Designed for ESP32-S3 (8MB PSRAM) and LVGL v8:
 * - Allocates truecolor RGB565 canvas buffers in PSRAM (MALLOC_CAP_SPIRAM).
 * - Offloads coordinate parsing and silhouette rasterization to a background
 *   FreeRTOS worker task on Core 0 to guarantee zero UI stutter or dropped frames.
 * - Auto-fit bounding box computation preserving strict 1:1 isometric aspect ratio.
 * - Cartesian to screen coordinate Y-axis inversion (right-side up geometry).
 * - Clean discrimination between rapid traverse (G0, unrendered) and cutting (G1, rendered).
 * - Cancellation token protecting against rapid file switching.
 * - Modular, reusable, and cleanly destructible.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gcode_preview_view gcode_preview_view_t;

typedef struct {
    float min_x, max_x;
    float min_y, max_y;
    float width_mm, height_mm;
    uint32_t total_lines;
    uint32_t cut_lines;
    uint32_t estimated_sec;
    char formatted_time[16]; // e.g. "03:45 min"
} gcode_preview_meta_t;

typedef void (*gcode_preview_ready_cb_t)(gcode_preview_view_t *view, const gcode_preview_meta_t *meta, void *user_data);

/**
 * @brief Creates a reusable G-code preview canvas widget.
 * @param parent Parent LVGL container
 * @param width Canvas width in pixels (e.g. 160)
 * @param height Canvas height in pixels (e.g. 170)
 * @return Opaque pointer to preview view object, or NULL on allocation failure.
 */
gcode_preview_view_t *gcode_preview_create(lv_obj_t *parent, int16_t width, int16_t height);

/**
 * @brief Gets the root container LVGL object of the preview (for layout positioning).
 */
lv_obj_t *gcode_preview_get_obj(gcode_preview_view_t *view);

/**
 * @brief Requests asynchronous background parsing and rendering of a G-code file.
 * Any running background preview is safely aborted.
 * @param view Preview view object
 * @param filepath Path to the G-code file
 * @param ready_cb Callback invoked on UI thread when preview completes
 * @param user_data User context pointer passed to callback
 */
void gcode_preview_load_file_async(gcode_preview_view_t *view, const char *filepath,
                                   gcode_preview_ready_cb_t ready_cb, void *user_data);

/**
 * @brief Synchronous rendering (used primarily in headless simulation or CLI testing).
 */
void gcode_preview_render_sync(gcode_preview_view_t *view, const char *filepath,
                               gcode_preview_ready_cb_t ready_cb, void *user_data);

/**
 * @brief Clears the preview canvas to default blank state with crosshairs.
 */
void gcode_preview_clear(gcode_preview_view_t *view);

/**
 * @brief Updates theme palette (dark mode vs light mode).
 */
void gcode_preview_set_theme(gcode_preview_view_t *view, bool is_dark);

/**
 * @brief Destroys the preview view, aborts active worker tasks, and frees PSRAM buffers.
 */
void gcode_preview_destroy(gcode_preview_view_t *view);

#ifdef __cplusplus
}
#endif

