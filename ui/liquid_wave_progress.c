/**
 * @file liquid_wave_progress.c
 * @brief Dynamic 3-Layer Liquid Wave Progress Component for Focal CNC.
 */

#include "liquid_wave_progress.h"
#include "focal_theme.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

static const char *TAG = "LIQUID_WAVE";

static void *wave_alloc(size_t size) {
#if defined(ESP_PLATFORM)
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) return ptr;
#endif
    return malloc(size);
}

static void wave_free(void *ptr) {
    if (!ptr) return;
#if defined(ESP_PLATFORM)
    heap_caps_free(ptr);
#else
    free(ptr);
#endif
}

struct liquid_wave_progress {
    lv_obj_t *container;
    lv_obj_t *canvas;
    lv_color_t *buf_base;     // Layer 1: Dim silhouette (20% opacity)
    lv_color_t *buf_active;   // Layer 2: Vibrant silhouette (100% Pastel Mint)
    lv_color_t *buf_display;  // Layer 3: Composite canvas buffer with wave mask
    int16_t w;
    int16_t h;
    float progress;           // 0.0f .. 1.0f
    float wave_phase;         // Radians (animating)
    bool is_dark;
    bool is_animating;
    lv_timer_t *anim_timer;
    char filepath[128];
};

// Fast inline pixel setter
static inline void wave_put_pixel(lv_color_t *buf, int w, int h, int x, int y, lv_color_t col) {
    if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) {
        buf[y * w + x] = col;
    }
}

// Bresenham line drawer with configurable thickness (1px or 2px)
static void wave_draw_line(lv_color_t *buf, int w, int h, int x0, int y0, int x1, int y1, lv_color_t col, int thickness) {
    int dx = abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        wave_put_pixel(buf, w, h, x0, y0, col);
        if (thickness > 1) {
            wave_put_pixel(buf, w, h, x0 + 1, y0, col);
            wave_put_pixel(buf, w, h, x0, y0 + 1, col);
            wave_put_pixel(buf, w, h, x0 + 1, y0 + 1, col);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static bool parse_gcode_coord(const char *line, char axis, float *out_val) {
    const char *p = line;
    while (*p) {
        if ((*p == axis || *p == (axis + 32)) && (p == line || *(p - 1) == ' ' || *(p - 1) == '\t')) {
            char *end;
            float v = strtof(p + 1, &end);
            if (end > p + 1) {
                *out_val = v;
                return true;
            }
        }
        p++;
    }
    return false;
}

// Clear surfaces with respective base and active backgrounds
static void wave_clear_surfaces(liquid_wave_progress_t *wp) {
    if (!wp || !wp->buf_base || !wp->buf_active) return;

    // Base background: dark charcoal or clean white
    lv_color_t bg_base = wp->is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xFAFAFC);
    lv_color_t grid_base = wp->is_dark ? lv_color_hex(0x20242E) : lv_color_hex(0xE5E7EB);

    // Active background: translucent fluid tint (deep emerald tint in dark, light mint in light)
    lv_color_t bg_active = wp->is_dark ? lv_color_hex(0x0A261D) : lv_color_hex(0xECFDF5);
    lv_color_t grid_active = wp->is_dark ? lv_color_hex(0x11382C) : lv_color_hex(0xD1FAE5);

    int total_pixels = wp->w * wp->h;
    for (int i = 0; i < total_pixels; i++) {
        wp->buf_base[i] = bg_base;
        wp->buf_active[i] = bg_active;
    }

    // Faint center crosshair lines
    int mid_x = wp->w / 2;
    int mid_y = wp->h / 2;
    for (int y = 4; y < wp->h - 4; y++) {
        wp->buf_base[y * wp->w + mid_x] = grid_base;
        wp->buf_active[y * wp->w + mid_x] = grid_active;
    }
    for (int x = 4; x < wp->w - 4; x++) {
        wp->buf_base[mid_y * wp->w + x] = grid_base;
        wp->buf_active[mid_y * wp->w + x] = grid_active;
    }
}

// Render G-code silhouette into both buf_base (20% dim) and buf_active (100% Pastel Mint)
static void wave_render_gcode(liquid_wave_progress_t *wp, const char *filepath) {
    if (!wp || !filepath || !filepath[0]) return;

    wave_clear_surfaces(wp);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        char alt[256];
        snprintf(alt, sizeof(alt), "/sdcard/%s", filepath);
        f = fopen(alt, "r");
    }
    if (!f) {
        // Fallback demo shape (stylized laser star/heart) if file cannot be read
        ESP_LOGW(TAG, "Cannot open '%s', drawing fallback silhouette", filepath);
        int cx = wp->w / 2;
        int cy = wp->h / 2;
        int r = wp->w / 3;
        lv_color_t col_dim = wp->is_dark ? lv_color_hex(0x2D3748) : lv_color_hex(0xD1D5DB);
        lv_color_t col_mint = wp->is_dark ? lv_color_hex(0x34D399) : lv_color_hex(0x10B981);
        for (int a = 0; a < 360; a += 10) {
            float rad1 = (float)a * 3.14159f / 180.0f;
            float rad2 = (float)(a + 10) * 3.14159f / 180.0f;
            int x1 = cx + (int)(r * cosf(rad1));
            int y1 = cy + (int)(r * sinf(rad1));
            int x2 = cx + (int)(r * cosf(rad2));
            int y2 = cy + (int)(r * sinf(rad2));
            wave_draw_line(wp->buf_base, wp->w, wp->h, x1, y1, x2, y2, col_dim, 1);
            wave_draw_line(wp->buf_active, wp->w, wp->h, x1, y1, x2, y2, col_mint, 2);
        }
        return;
    }

    // PASS 1: Calculate G1 workpiece bounding box
    char line[128];
    float cur_x = 0.0f, cur_y = 0.0f;
    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;
    bool has_g1 = false;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '\r' || line[0] == '\n') continue;
        bool is_g1 = (strstr(line, "G1") != NULL || strstr(line, "g1") != NULL);
        bool is_g0 = (strstr(line, "G0") != NULL || strstr(line, "g0") != NULL);
        if (!is_g0 && !is_g1) continue;

        float nx = cur_x, ny = cur_y;
        parse_gcode_coord(line, 'X', &nx);
        parse_gcode_coord(line, 'Y', &ny);

        if (is_g1) {
            if (nx < min_x) min_x = nx;
            if (nx > max_x) max_x = nx;
            if (ny < min_y) min_y = ny;
            if (ny > max_y) max_y = ny;
            has_g1 = true;
        }
        cur_x = nx;
        cur_y = ny;
    }

    if (!has_g1 || (max_x <= min_x) || (max_y <= min_y)) {
        min_x = 0; max_x = 100;
        min_y = 0; max_y = 100;
    }

    float width_mm = max_x - min_x;
    float height_mm = max_y - min_y;

    // Auto-fit math with 1:1 isometric aspect ratio preservation
    const float margin = 10.0f;
    float avail_w = (float)wp->w - 2.0f * margin;
    float avail_h = (float)wp->h - 2.0f * margin;
    float scale = (avail_w / width_mm < avail_h / height_mm) ? (avail_w / width_mm) : (avail_h / height_mm);

    float drawn_w = width_mm * scale;
    float drawn_h = height_mm * scale;
    float pad_x = margin + (avail_w - drawn_w) / 2.0f;
    float pad_y = margin + (avail_h - drawn_h) / 2.0f;

    // PASS 2: Rasterize cutting silhouette into buf_base and buf_active
    fseek(f, 0, SEEK_SET);
    cur_x = 0.0f;
    cur_y = 0.0f;

    // Palette tokens
    lv_color_t col_dim = wp->is_dark ? lv_color_hex(0x263040) : lv_color_hex(0xD1D5DB);
    lv_color_t col_mint = wp->is_dark ? lv_color_hex(0x34D399) : lv_color_hex(0x10B981);

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '\r' || line[0] == '\n') continue;
        bool is_g1 = (strstr(line, "G1") != NULL || strstr(line, "g1") != NULL);
        bool is_g0 = (strstr(line, "G0") != NULL || strstr(line, "g0") != NULL);
        if (!is_g0 && !is_g1) continue;

        float nx = cur_x, ny = cur_y;
        parse_gcode_coord(line, 'X', &nx);
        parse_gcode_coord(line, 'Y', &ny);

        if (is_g1) {
            // Y-axis inversion: Screen Y=0 is top, CNC Y=0 is bottom
            int px0 = (int)(pad_x + (cur_x - min_x) * scale);
            int py0 = (int)(pad_y + (max_y - cur_y) * scale);
            int px1 = (int)(pad_x + (nx - min_x) * scale);
            int py1 = (int)(pad_y + (max_y - ny) * scale);

            // Layer 1: Base dim silhouette (1px)
            wave_draw_line(wp->buf_base, wp->w, wp->h, px0, py0, px1, py1, col_dim, 1);
            // Layer 2: Content revealed silhouette (2px vibrant Pastel Mint)
            wave_draw_line(wp->buf_active, wp->w, wp->h, px0, py0, px1, py1, col_mint, 2);
        }
        cur_x = nx;
        cur_y = ny;
    }

    fclose(f);
}

// Composite Layer 1 + Layer 2 using Layer 3 Wave Mask into buf_display
static void wave_composite_frame(liquid_wave_progress_t *wp) {
    if (!wp || !wp->buf_base || !wp->buf_active || !wp->buf_display) return;

    int W = wp->w;
    int H = wp->h;
    float p = wp->progress;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;

    // Nominal liquid level (0% at bottom y=H-1, 100% at top y=0)
    float y_level = (float)(H - 1) * (1.0f - p);

    // Surface wave damping near top and bottom boundaries
    float damp = 1.0f;
    if (p < 0.08f) damp = p / 0.08f;
    else if (p > 0.92f) damp = (1.0f - p) / 0.08f;
    if (damp < 0.0f) damp = 0.0f;

    float A1 = 3.8f * damp;
    float A2 = 1.4f * damp;
    float lambda1 = (float)W * 0.75f;
    float lambda2 = (float)W * 0.38f;
    float phi = wp->wave_phase;

    // Crest glow colors
    lv_color_t crest_col = wp->is_dark ? lv_color_hex(0xA7F3D0) : lv_color_hex(0x6EE7B7);
    lv_color_t crest_subtle = wp->is_dark ? lv_color_hex(0x34D399) : lv_color_hex(0x10B981);

    for (int x = 0; x < W; x++) {
        // Calculate dynamic wave surface height for column x
        float sin_v = sinf((2.0f * 3.14159265f * (float)x / lambda1) + phi);
        float cos_v = cosf((2.0f * 3.14159265f * (float)x / lambda2) - 1.4f * phi);
        int wave_y = (int)roundf(y_level + A1 * sin_v + A2 * cos_v);
        if (wave_y < 0) wave_y = 0;
        if (wave_y >= H) wave_y = H - 1;

        for (int y = 0; y < H; y++) {
            int idx = y * W + x;
            if (y < wave_y - 1) {
                // Above wave: Layer 1 Base (20% dim silhouette)
                wp->buf_display[idx] = wp->buf_base[idx];
            } else if (y == wave_y - 1 && damp > 0.1f) {
                // Subtle crest glow
                wp->buf_display[idx] = crest_subtle;
            } else if (y == wave_y && damp > 0.05f) {
                // Luminous primary wave crest surface line
                wp->buf_display[idx] = crest_col;
            } else {
                // Below wave: Layer 2 Revealed (100% vibrant Pastel Mint)
                wp->buf_display[idx] = wp->buf_active[idx];
            }
        }
    }

    lv_obj_invalidate(wp->canvas);
}

static void wave_anim_timer_cb(lv_timer_t *timer) {
    liquid_wave_progress_t *wp = (liquid_wave_progress_t *)timer->user_data;
    if (!wp) return;

    // Solo animar y recalcular si la animación está activa y el contenedor está visible
    if (!wp->is_animating) return;
    if (wp->container && lv_obj_has_flag_any(wp->container, LV_OBJ_FLAG_HIDDEN)) return;

    wp->wave_phase = fmodf(wp->wave_phase + 0.12f, 2.0f * 3.14159265f);
    wave_composite_frame(wp);
}

liquid_wave_progress_t *liquid_wave_progress_create(lv_obj_t *parent, int16_t width, int16_t height) {
    liquid_wave_progress_t *wp = (liquid_wave_progress_t *)calloc(1, sizeof(liquid_wave_progress_t));
    if (!wp) return NULL;

    wp->w = width;
    wp->h = height;
    wp->progress = 0.45f; // Default 45% progress
    wp->wave_phase = 0.0f;
    wp->is_dark = focal_theme_is_dark();
    wp->is_animating = false; // Solo animar cuando haya un corte en ejecucion
    strncpy(wp->filepath, "prueba_mario.gcode", sizeof(wp->filepath) - 1);

    size_t buf_size = width * height * sizeof(lv_color_t);
    wp->buf_base = (lv_color_t *)wave_alloc(buf_size);
    wp->buf_active = (lv_color_t *)wave_alloc(buf_size);
    wp->buf_display = (lv_color_t *)wave_alloc(buf_size);

    if (!wp->buf_base || !wp->buf_active || !wp->buf_display) {
        ESP_LOGE(TAG, "Failed to allocate liquid wave buffers (%zu bytes each)", buf_size);
        liquid_wave_progress_destroy(wp);
        return NULL;
    }

    // Outer container squircle card
    wp->container = lv_obj_create(parent);
    lv_obj_set_size(wp->container, width + 4, height + 4);
    lv_obj_set_style_pad_all(wp->container, 1, 0);
    lv_obj_set_style_radius(wp->container, FOCAL_RADIUS_M, 0);
    lv_obj_set_style_border_width(wp->container, 1, 0);
    lv_obj_set_style_border_color(wp->container,
        wp->is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xD1D5DB), 0);
    lv_obj_set_style_bg_color(wp->container,
        wp->is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xFAFAFC), 0);
    lv_obj_clear_flag(wp->container, LV_OBJ_FLAG_SCROLLABLE);

    // LVGL Canvas bound to buf_display
    wp->canvas = lv_canvas_create(wp->container);
    lv_canvas_set_buffer(wp->canvas, wp->buf_display, width, height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(wp->canvas);

    // Initial G-code render & composite
    wave_render_gcode(wp, wp->filepath);
    wave_composite_frame(wp);

    // Timer de animación suave a 22 FPS (45 ms) con bajo consumo de CPU
    wp->anim_timer = lv_timer_create(wave_anim_timer_cb, 45, wp);

    return wp;
}

lv_obj_t *liquid_wave_progress_get_obj(liquid_wave_progress_t *wp) {
    return wp ? wp->container : NULL;
}

void liquid_wave_progress_load_file(liquid_wave_progress_t *wp, const char *filepath) {
    if (!wp || !filepath) return;
    strncpy(wp->filepath, filepath, sizeof(wp->filepath) - 1);
    wave_render_gcode(wp, wp->filepath);
    wave_composite_frame(wp);
}

void liquid_wave_progress_set_progress(liquid_wave_progress_t *wp, float progress) {
    if (!wp) return;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    wp->progress = progress;
    wave_composite_frame(wp);
}

void liquid_wave_progress_set_animating(liquid_wave_progress_t *wp, bool animating) {
    if (!wp) return;
    wp->is_animating = animating;
}

void liquid_wave_progress_set_theme(liquid_wave_progress_t *wp, bool is_dark) {
    if (!wp) return;
    wp->is_dark = is_dark;
    if (wp->container) {
        lv_obj_set_style_border_color(wp->container,
            is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xD1D5DB), 0);
        lv_obj_set_style_bg_color(wp->container,
            is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xFAFAFC), 0);
    }
    wave_render_gcode(wp, wp->filepath);
    wave_composite_frame(wp);
}

void liquid_wave_progress_destroy(liquid_wave_progress_t *wp) {
    if (!wp) return;
    if (wp->anim_timer) {
        lv_timer_del(wp->anim_timer);
        wp->anim_timer = NULL;
    }
    if (wp->buf_base) wave_free(wp->buf_base);
    if (wp->buf_active) wave_free(wp->buf_active);
    if (wp->buf_display) wave_free(wp->buf_display);
    if (wp->container && lv_obj_is_valid(wp->container)) {
        lv_obj_del(wp->container);
    }
    free(wp);
}

