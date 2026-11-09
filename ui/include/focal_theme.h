/**
 * @file focal_theme.h
 * @brief Focal CNC Design System - LVGL Theme Tokens
 *
 * Implements strict tokens defined in focal-design-system skill:
 * - High-contrast minimal dark mode (bgApp #0F1115, bgCard #1A1D24)
 * - Zero saturated generic colors (calibrated pastels: blue, mint, amber, red)
 * - Zero emojis (clean vector/symbol shapes)
 * - Wide squircle curvature (radiusS: 8, radiusM: 14, radiusL: 20, radiusPill: 999)
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Color Tokens (Modo Oscuro Oficial Focal)
// =============================================================================
#define FOCAL_HEX_BG_APP             0x0F1115
#define FOCAL_HEX_BG_CARD            0x1A1D24
#define FOCAL_HEX_BG_CARD_HOVER      0x222630
#define FOCAL_HEX_BG_SUBTLE          0x262A35
#define FOCAL_HEX_BORDER_SUBTLE      0x2D333F

#define FOCAL_HEX_PASTEL_BLUE        0x60A5FA
#define FOCAL_HEX_PASTEL_BLUE_SUBTLE 0x1E293B
#define FOCAL_HEX_PASTEL_BLUE_BORDER 0x2563EB

#define FOCAL_HEX_PASTEL_MINT        0x34D399
#define FOCAL_HEX_PASTEL_MINT_SUBTLE 0x064E3B
#define FOCAL_HEX_PASTEL_MINT_BORDER 0x059669

#define FOCAL_HEX_PASTEL_AMBER       0xFBBF24
#define FOCAL_HEX_PASTEL_AMBER_SUBTLE 0x451A03
#define FOCAL_HEX_PASTEL_AMBER_BORDER 0xB45309

#define FOCAL_HEX_PASTEL_RED         0xF87171
#define FOCAL_HEX_PASTEL_RED_SUBTLE  0x450A0A
#define FOCAL_HEX_PASTEL_RED_BORDER  0xDC2626

#define FOCAL_HEX_TEXT_PRIMARY       0xF9FAFB
#define FOCAL_HEX_TEXT_SECONDARY     0x9CA3AF
#define FOCAL_HEX_TEXT_TERTIARY      0x6B7280
#define FOCAL_HEX_TEXT_INVERSE       0x111827

// Helpers lv_color_t
#define FOCAL_COLOR_BG_APP             lv_color_hex(FOCAL_HEX_BG_APP)
#define FOCAL_COLOR_BG_CARD            lv_color_hex(FOCAL_HEX_BG_CARD)
#define FOCAL_COLOR_BG_CARD_HOVER      lv_color_hex(FOCAL_HEX_BG_CARD_HOVER)
#define FOCAL_COLOR_BG_SUBTLE          lv_color_hex(FOCAL_HEX_BG_SUBTLE)
#define FOCAL_COLOR_BORDER_SUBTLE      lv_color_hex(FOCAL_HEX_BORDER_SUBTLE)

#define FOCAL_COLOR_PASTEL_BLUE        lv_color_hex(FOCAL_HEX_PASTEL_BLUE)
#define FOCAL_COLOR_PASTEL_BLUE_SUBTLE lv_color_hex(FOCAL_HEX_PASTEL_BLUE_SUBTLE)
#define FOCAL_COLOR_PASTEL_BLUE_BORDER lv_color_hex(FOCAL_HEX_PASTEL_BLUE_BORDER)

#define FOCAL_COLOR_PASTEL_MINT        lv_color_hex(FOCAL_HEX_PASTEL_MINT)
#define FOCAL_COLOR_PASTEL_MINT_SUBTLE lv_color_hex(FOCAL_HEX_PASTEL_MINT_SUBTLE)
#define FOCAL_COLOR_PASTEL_MINT_BORDER lv_color_hex(FOCAL_HEX_PASTEL_MINT_BORDER)

#define FOCAL_COLOR_PASTEL_AMBER       lv_color_hex(FOCAL_HEX_PASTEL_AMBER)
#define FOCAL_COLOR_PASTEL_AMBER_SUBTLE lv_color_hex(FOCAL_HEX_PASTEL_AMBER_SUBTLE)
#define FOCAL_COLOR_PASTEL_AMBER_BORDER lv_color_hex(FOCAL_HEX_PASTEL_AMBER_BORDER)

#define FOCAL_COLOR_PASTEL_RED         lv_color_hex(FOCAL_HEX_PASTEL_RED)
#define FOCAL_COLOR_PASTEL_RED_SUBTLE  lv_color_hex(FOCAL_HEX_PASTEL_RED_SUBTLE)
#define FOCAL_COLOR_PASTEL_RED_BORDER  lv_color_hex(FOCAL_HEX_PASTEL_RED_BORDER)

#define FOCAL_COLOR_TEXT_PRIMARY       lv_color_hex(FOCAL_HEX_TEXT_PRIMARY)
#define FOCAL_COLOR_TEXT_SECONDARY     lv_color_hex(FOCAL_HEX_TEXT_SECONDARY)
#define FOCAL_COLOR_TEXT_TERTIARY      lv_color_hex(FOCAL_HEX_TEXT_TERTIARY)
#define FOCAL_COLOR_TEXT_INVERSE       lv_color_hex(FOCAL_HEX_TEXT_INVERSE)

// =============================================================================
// Radios de Esquina Estandarizados (Squircles)
// =============================================================================
#define FOCAL_RADIUS_S     8
#define FOCAL_RADIUS_M     14
#define FOCAL_RADIUS_L     20
#define FOCAL_RADIUS_XL    28
#define FOCAL_RADIUS_PILL  LV_RADIUS_CIRCLE

// Estilos globales de UI
void focal_theme_init(void);
void focal_theme_set_dark(bool is_dark);
bool focal_theme_is_dark(void);
void focal_theme_set_accent(uint8_t accent_idx);
uint8_t focal_theme_get_accent_idx(void);
uint32_t focal_theme_get_accent_hex(void);
lv_color_t focal_theme_get_accent_color(void);

// Estilos compartidos
extern lv_style_t focal_style_screen;
extern lv_style_t focal_style_card;
extern lv_style_t focal_style_pill;
extern lv_style_t focal_style_btn_primary;
extern lv_style_t focal_style_btn_secondary;
extern lv_style_t focal_style_btn_mint;
extern lv_style_t focal_style_btn_amber;
extern lv_style_t focal_style_btn_danger;

#ifdef __cplusplus
}
#endif
