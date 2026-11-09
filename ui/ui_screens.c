/**
 * @file ui_screens.c
 * @brief Industrial UI Screen Manager for Focal CNC Laser Cutter (LVGL v8)
 *
 * Implements strict focal-design-system guidelines:
 * - High-speed instant screen switching (zero slow software fade animations)
 * - 28 anti-aliased vector icons (focal_icons.h)
 * - Master-Detail list navigation for Configuration Menu
 * - Dynamic Dark / Light theme switching & accent color selector with NVS
 * persistence
 * - Pure Station Wi-Fi manager with real scanning, scrollable network list, and
 * visible keyboard input modal
 * - Quick Cut module with simulated 160x200 mm bed and real-time touch dragging
 * - Visual shape buttons with vector icon drawings (no technical jargon)
 * - Clean ASCII strings (zero 'ñ' or broken accents)
 */

#include "ui_screens.h"
#include "esp_log.h"
#include "focal_icons.h"
#include "focal_theme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gcode_parser.h"
#include "ili9488_lvgl.h"
#include "laser_driver.h"
#include "settings_manager.h"
#include "wifi_manager.h"
#if defined(ESP_PLATFORM)
#include "sd_card.h"
#endif
#include "gcode_preview.h"
#include "liquid_wave_progress.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "FOCAL_UI";

#define FOCAL_FONT &lv_font_montserrat_14

// Global Theme Styles
lv_style_t focal_style_screen;
lv_style_t focal_style_card;
lv_style_t focal_style_pill;
lv_style_t focal_style_btn_primary;
lv_style_t focal_style_btn_secondary;
lv_style_t focal_style_btn_mint;
lv_style_t focal_style_btn_amber;
lv_style_t focal_style_btn_danger;

// Theme state
static bool s_is_dark = true;
static uint8_t s_accent_idx =
    0; // 0: Blue, 1: Mint, 2: Amber, 3: Purple, 4: Red

static const uint32_t s_accent_dark_hex[5] = {
    FOCAL_HEX_PASTEL_BLUE,  // 0x60A5FA
    FOCAL_HEX_PASTEL_MINT,  // 0x34D399
    FOCAL_HEX_PASTEL_AMBER, // 0xFBBF24
    0xA78BFA,               // Pastel Purple
    FOCAL_HEX_PASTEL_RED    // 0xF87171
};

static const uint32_t s_accent_light_hex[5] = {
    0x3B82F6, // Light Blue
    0x10B981, // Light Mint
    0xF59E0B, // Light Amber
    0x8B5CF6, // Light Purple
    0xEF4444  // Light Red
};

// Screens array
static lv_obj_t *s_screens[UI_SCREEN_MAX] = {0};
static ui_screen_id_t s_current_screen = UI_SCREEN_LAUNCHER;

// Header handles
static lv_obj_t *s_hdrs[UI_SCREEN_MAX] = {0};
static lv_obj_t *s_hdr_titles[UI_SCREEN_MAX] = {0};
static lv_obj_t *s_hdr_pills[UI_SCREEN_MAX] = {0};
static lv_obj_t *s_hdr_wifi_icons[UI_SCREEN_MAX] = {0};

// Launcher handles
static lv_obj_t *s_launcher_badges[6] = {0};
static lv_obj_t *s_launcher_titles[6] = {0};

// Forward screen creators
static void create_launcher_screen(void);
static void create_jogging_screen(void);
static void create_sd_screen(void);
static void create_cut_monitor_screen(void);
static void create_calibration_screen(void);
static void create_shapes_screen(void);
static void create_settings_screen(void);
static void create_bed_dimensions_screen(void);

// Machine Bed Dimensions and Orientation state (in mm and deg)
static int s_bed_w_mm = 160;
static int s_bed_h_mm = 200;
static int s_bed_rot = 0; // 0, 90, 180, 270

// Settings handles and state
static int s_settings_tab = 0; // 0: WiFi, 1: Apariencia, 2: Laser, 3: SD, 4: Cinematica, 5: Sistema
static lv_obj_t *s_settings_panels[6] = {0};
static lv_obj_t *s_settings_menu_btns[6] = {0};
static lv_obj_t *s_wifi_cur_card = NULL;
static lv_obj_t *s_lbl_wifi_detail_ssid = NULL;
static lv_obj_t *s_lbl_wifi_detail_sep = NULL;
static lv_obj_t *s_lbl_wifi_detail = NULL;
static lv_obj_t *s_wifi_detail_icon = NULL;
static lv_obj_t *s_btn_wifi_scan = NULL;

static lv_obj_t *s_lbl_laser_pwr = NULL;
static lv_obj_t *s_slider_laser_pwr = NULL;
static int s_laser_power_val = 100;

static lv_obj_t *s_lbl_kin_feed = NULL;
static lv_obj_t *s_slider_kin_feed = NULL;
static lv_obj_t *s_lbl_kin_bed_info = NULL;
static lv_obj_t *s_btn_kin_rot = NULL;

static lv_obj_t *s_settings_confirm_modal = NULL;
static lv_obj_t *s_confirm_title_lbl = NULL;
static lv_obj_t *s_confirm_msg_lbl = NULL;
static lv_obj_t *s_confirm_btn_ok = NULL;
static lv_obj_t *s_confirm_icon = NULL;
static void (*s_confirm_action_cb)(void) = NULL;

static lv_obj_t *s_legal_modal = NULL;

// i18n Translatable Label Pointers
static lv_obj_t *s_hdr_back_lbls[UI_SCREEN_MAX] = {0};
static lv_obj_t *s_launcher_title_lbls[6] = {0};
static lv_obj_t *s_btn_zero_lbl = NULL;
static lv_obj_t *s_jog_lock_title_lbl = NULL;
static lv_obj_t *s_jog_lock_desc_lbl = NULL;
static lv_obj_t *s_jog_lock_gomon_lbl = NULL;
static lv_obj_t *s_jog_lock_menu_lbl = NULL;
static lv_obj_t *s_btn_run_cut_lbl = NULL;
static lv_obj_t *s_cut_modal_title_lbl = NULL;
static lv_obj_t *s_cut_modal_cancel_lbl = NULL;
static lv_obj_t *s_cut_modal_start_lbl = NULL;
static lv_obj_t *s_sd_opt_title_lbl = NULL;
static lv_obj_t *s_sd_opt_fav_lbl = NULL;
static lv_obj_t *s_sd_opt_del_lbl = NULL;
static lv_obj_t *s_sd_opt_canc_lbl = NULL;
static lv_obj_t *s_sd_del_title_lbl = NULL;
static lv_obj_t *s_sd_del_canc_lbl = NULL;
static lv_obj_t *s_sd_del_conf_lbl = NULL;
static lv_obj_t *s_mon_btn_stop_lbl = NULL;
static lv_obj_t *s_mon_btn_pause_lbl = NULL;
static lv_obj_t *s_stop_modal_title_lbl = NULL;
static lv_obj_t *s_stop_modal_msg_lbl = NULL;
static lv_obj_t *s_stop_modal_cont_lbl = NULL;
static lv_obj_t *s_stop_modal_stop_lbl = NULL;
static lv_obj_t *s_calib_lbl_bed = NULL;
static lv_obj_t *s_calib_btn_pointer_lbl = NULL;
static lv_obj_t *s_calib_btn_reset_lbl = NULL;
static lv_obj_t *s_calib_btn_prev_lbl = NULL;
static lv_obj_t *s_calib_btn_next_lbl = NULL;
static lv_obj_t *s_calib_succ_title_lbl = NULL;
static lv_obj_t *s_calib_succ_desc_lbl = NULL;
static lv_obj_t *s_calib_succ_ok_lbl = NULL;
static lv_obj_t *s_shp_btn_center_lbl = NULL;
static lv_obj_t *s_shp_btn_origin_lbl = NULL;
static lv_obj_t *s_shp_btn_frame_lbl = NULL;
static lv_obj_t *s_shp_btn_cut_lbl = NULL;
static lv_obj_t *s_settings_menu_lbls[6] = {0};
static lv_obj_t *s_wifi_btn_scan_lbl = NULL;
static lv_obj_t *s_wifi_btn_forget_lbl = NULL;
static lv_obj_t *s_thm_lbl_title = NULL;
static lv_obj_t *s_thm_lbl_dark = NULL;
static lv_obj_t *s_thm_lbl_light = NULL;
static lv_obj_t *s_thm_lbl_accent = NULL;
static lv_obj_t *s_lz_lbl_title = NULL;
static lv_obj_t *s_lz_btn_pulse_lbl = NULL;
static lv_obj_t *s_sd_lbl_title = NULL;
static lv_obj_t *s_lbl_ovr = NULL;
static lv_obj_t *s_btn_sd_fmt_lbl = NULL;
static lv_obj_t *s_kin_lbl_title = NULL;
static lv_obj_t *s_kin_lbl_bed_sec = NULL;
static lv_obj_t *s_btn_kin_rot_lbl = NULL;
static lv_obj_t *s_btn_kin_dim_lbl = NULL;
static lv_obj_t *s_btn_save_kin_lbl = NULL;
static lv_obj_t *s_sys_lbl_title = NULL;
static lv_obj_t *s_btn_legal_lbl = NULL;
static lv_obj_t *s_btn_rst_lbl = NULL;
static lv_obj_t *s_bed_dim_lbl_prev_title = NULL;
static lv_obj_t *s_bdim_btn_canc_lbl = NULL;
static lv_obj_t *s_bdim_btn_ok_lbl = NULL;
static lv_obj_t *s_bdim_modal_title_lbl = NULL;
static lv_obj_t *s_bdim_modal_canc_lbl = NULL;
static lv_obj_t *s_bdim_modal_ok_lbl = NULL;

// Bed Dimensions Screen Edit handles
static int s_edit_bed_w = 160;
static int s_edit_bed_h = 200;
static int s_edit_active_field = 0; // 0: X (width), 1: Y (height)
static char s_keypad_buf[16] = "160";
static lv_obj_t *s_bed_dim_card_prev = NULL;
static lv_obj_t *s_bed_dim_sim_box = NULL;
static lv_obj_t *s_bed_dim_lbl_info = NULL;
static lv_obj_t *s_bed_dim_btn_x = NULL;
static lv_obj_t *s_bed_dim_btn_y = NULL;
static lv_obj_t *s_bed_dim_lbl_x = NULL;
static lv_obj_t *s_bed_dim_lbl_y = NULL;
static lv_obj_t *s_bed_dim_confirm_modal = NULL;
static void update_bed_dimensions_preview(void);

static bool s_is_cutting = false;
static gcode_preview_view_t *s_sd_preview = NULL;
static gcode_preview_view_t *s_shapes_bed_preview = NULL;
static liquid_wave_progress_t *s_cut_liquid_wave = NULL;
static lv_obj_t *s_mon_lbl_pct = NULL;
static lv_obj_t *s_mon_lbl_lines = NULL;
static lv_obj_t *s_mon_lbl_filename = NULL;
static lv_obj_t *s_mon_lbl_elapsed = NULL;
static lv_obj_t *s_mon_lbl_remaining = NULL;
static lv_obj_t *s_mon_btn_led = NULL;
static lv_obj_t *s_mon_btn_pause_resume = NULL;
static lv_obj_t *s_mon_btn_stop = NULL;
static lv_obj_t *s_stop_modal = NULL;
static bool s_machine_light_on = false;
static void update_mon_pause_btn(bool is_cutting);
static char s_selected_file[64] = "prueba_mario.gcode";

// Forward UI state handles for dynamic theme updating
static lv_obj_t *s_bed_obj = NULL;
static lv_obj_t *s_bed_shape_preview = NULL;
static lv_obj_t *s_bed_triangle_line = NULL;
static lv_obj_t *s_calib_bed_box = NULL;
static lv_obj_t *s_calib_lbl_bed_dim = NULL;
static lv_obj_t *s_calib_success_modal = NULL;
static int s_calib_step = 0;
static lv_obj_t *s_calib_pts[5] = {0};
static lv_obj_t *s_calib_laser_dot = NULL;
static void update_jog_bed_geometry(void);
static void update_calibration_bed_geometry(void);
static void update_shapes_bed_geometry(void);
static void update_bed_shape_position(void);
static lv_obj_t *s_wifi_list = NULL;
static lv_obj_t *s_wifi_modal = NULL;
static lv_obj_t *s_shape_sel_btns[4] = {0};
static int s_shape_type = 0;

// =============================================================================
// VECTOR ICON & FLEX BUTTON HELPERS
// =============================================================================
static lv_obj_t *focal_create_icon(lv_obj_t *parent,
                                   const lv_img_dsc_t *icon_src,
                                   uint32_t color_hex) {
  lv_obj_t *img = lv_img_create(parent);
  lv_img_set_src(img, icon_src);
  lv_obj_set_style_img_recolor(img, lv_color_hex(color_hex), 0);
  lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
  return img;
}

static lv_obj_t *focal_create_btn(lv_obj_t *parent, int w, int h,
                                  const lv_img_dsc_t *icon, const char *text,
                                  lv_style_t *style, uint32_t ic_color) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, h);
  if (style)
    lv_obj_add_style(btn, style, 0);
  lv_obj_set_style_radius(btn, FOCAL_RADIUS_M, 0);
  lv_obj_set_style_pad_all(btn, 4, 0);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btn, 6, 0);

  if (icon) {
    lv_obj_t *img = focal_create_icon(btn, icon, ic_color);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
  }
  if (text) {
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(lbl, FOCAL_FONT, 0);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    if (w >= 160) {
      lv_obj_set_flex_grow(lbl, 1);
    }
  }
  return btn;
}

static void on_nav_back_cb(lv_event_t *e) {
  if (s_current_screen == UI_SCREEN_BED_DIMENSIONS) {
    ui_switch_screen(UI_SCREEN_SETTINGS);
    ui_settings_select_tab(4);
  } else {
    ui_switch_screen(UI_SCREEN_LAUNCHER);
  }
}

// Common Header builder
static lv_obj_t *create_screen_header(ui_screen_id_t screen_id,
                                      const char *title, bool show_back) {
  lv_obj_t *screen = s_screens[screen_id];
  lv_obj_t *hdr = lv_obj_create(screen);
  lv_obj_set_size(hdr, 480, 40);
  lv_obj_set_pos(hdr, 0, 0);
  lv_obj_set_style_bg_color(
      hdr, s_is_dark ? lv_color_hex(0x0F1115) : lv_color_hex(0xF5F6F8), 0);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(
      hdr, s_is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xD1D5DB), 0);
  lv_obj_set_style_border_width(hdr, 1, 0);
  lv_obj_set_style_radius(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 4, 0);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

  if (show_back) {
    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 74, 30);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_style(btn_back, &focal_style_btn_secondary, 0);
    lv_obj_set_style_radius(btn_back, FOCAL_RADIUS_S, 0);
    lv_obj_set_style_pad_all(btn_back, 2, 0);
    lv_obj_set_flex_flow(btn_back, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_back, 4, 0);
    lv_obj_add_event_cb(btn_back, on_nav_back_cb, LV_EVENT_CLICKED, NULL);

    focal_create_icon(btn_back, &focal_icon_back,
                      s_is_dark ? FOCAL_HEX_PASTEL_BLUE : 0x3B82F6);
    lv_obj_t *lbl_b = lv_label_create(btn_back);
    lv_label_set_text(lbl_b, "Menu");
    lv_obj_set_style_text_font(lbl_b, FOCAL_FONT, 0);
    s_hdr_back_lbls[screen_id] = lbl_b;
  }

  lv_obj_t *lbl_title = lv_label_create(hdr);
  lv_label_set_text(lbl_title, title);
  lv_obj_set_style_text_font(lbl_title, FOCAL_FONT, 0);
  lv_obj_set_style_text_color(
      lbl_title, s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827),
      0);
  if (show_back) {
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 90, 0);
  } else {
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 12, 0);
  }
  s_hdrs[screen_id] = hdr;
  s_hdr_titles[screen_id] = lbl_title;

  // Status Pill on top-right (Wi-Fi status with connected SSID and ellipsis)
  lv_obj_t *pill = lv_obj_create(hdr);
  lv_obj_set_size(pill, 142, 28);
  lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -6, 0);
  lv_obj_add_style(pill, &focal_style_pill, 0);
  lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(pill, 6, 0);
  lv_obj_set_style_pad_hor(pill, 8, 0);
  lv_obj_set_style_pad_ver(pill, 2, 0);

  bool is_conn = wifi_manager_is_sta_connected();
  const char *cur_ssid = is_conn ? wifi_manager_get_sta_ssid() : "Sin Wi-Fi";
  uint32_t ic_color = is_conn ? FOCAL_HEX_PASTEL_MINT : 0x9CA3AF;

  lv_obj_t *ic_wifi = focal_create_icon(pill, &focal_icon_wifi, ic_color);
  lv_obj_clear_flag(ic_wifi, LV_OBJ_FLAG_CLICKABLE);
  s_hdr_wifi_icons[screen_id] = ic_wifi;

  lv_obj_t *lbl_st = lv_label_create(pill);
  lv_obj_set_size(lbl_st, 96, 18);
  lv_label_set_long_mode(lbl_st, LV_LABEL_LONG_DOT);
  lv_label_set_text(lbl_st, cur_ssid);
  lv_obj_set_style_text_font(lbl_st, FOCAL_FONT, 0);
  lv_obj_set_style_text_color(
      lbl_st, s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
  lv_obj_clear_flag(lbl_st, LV_OBJ_FLAG_CLICKABLE);
  s_hdr_pills[screen_id] = lbl_st;

  return hdr;
}

// =============================================================================
// THEME & STYLES MANAGER (DARK / LIGHT & ACCENT)
// =============================================================================
static void focal_theme_update_styles(void) {
  uint32_t bg_app = s_is_dark ? 0x0F1115 : 0xF5F6F8;
  uint32_t bg_card = s_is_dark ? 0x1A1D24 : 0xFFFFFF;
  uint32_t bg_subtle = s_is_dark ? 0x262A35 : 0xEFF1F5;
  uint32_t border_subtle = s_is_dark ? 0x2D333F : 0xD1D5DB;
  uint32_t text_primary = s_is_dark ? 0xF9FAFB : 0x111827;
  uint32_t accent_hex = s_is_dark ? s_accent_dark_hex[s_accent_idx]
                                  : s_accent_light_hex[s_accent_idx];

  lv_style_set_bg_color(&focal_style_screen, lv_color_hex(bg_app));
  lv_style_set_text_color(&focal_style_screen, lv_color_hex(text_primary));

  lv_style_set_bg_color(&focal_style_card, lv_color_hex(bg_card));
  lv_style_set_border_color(&focal_style_card, lv_color_hex(border_subtle));
  lv_style_set_text_color(&focal_style_card, lv_color_hex(text_primary));

  lv_style_set_bg_color(&focal_style_pill, lv_color_hex(bg_subtle));
  lv_style_set_border_color(&focal_style_pill, lv_color_hex(border_subtle));
  lv_style_set_text_color(&focal_style_pill, lv_color_hex(text_primary));

  lv_style_set_bg_color(&focal_style_btn_secondary, lv_color_hex(bg_card));
  lv_style_set_border_color(&focal_style_btn_secondary,
                            lv_color_hex(border_subtle));
  lv_style_set_text_color(&focal_style_btn_secondary,
                          lv_color_hex(text_primary));

  lv_style_set_bg_color(&focal_style_btn_primary, lv_color_hex(accent_hex));

  lv_obj_report_style_change(NULL);
}

void focal_theme_init(void) {
  machine_settings_t cfg;
  settings_manager_get(&cfg);
  s_is_dark = (cfg.theme_mode == 0);
  s_accent_idx = (cfg.accent_color_idx < 5) ? cfg.accent_color_idx : 0;

  lv_style_init(&focal_style_screen);
  lv_style_set_bg_opa(&focal_style_screen, LV_OPA_COVER);

  lv_style_init(&focal_style_card);
  lv_style_set_bg_opa(&focal_style_card, LV_OPA_COVER);
  lv_style_set_radius(&focal_style_card, FOCAL_RADIUS_M);
  lv_style_set_border_width(&focal_style_card, 1);
  lv_style_set_pad_all(&focal_style_card, 6);

  lv_style_init(&focal_style_pill);
  lv_style_set_bg_opa(&focal_style_pill, LV_OPA_COVER);
  lv_style_set_radius(&focal_style_pill, FOCAL_RADIUS_PILL);
  lv_style_set_border_width(&focal_style_pill, 1);
  lv_style_set_pad_hor(&focal_style_pill, 8);
  lv_style_set_pad_ver(&focal_style_pill, 2);

  lv_style_init(&focal_style_btn_primary);
  lv_style_set_bg_opa(&focal_style_btn_primary, LV_OPA_COVER);
  lv_style_set_radius(&focal_style_btn_primary, FOCAL_RADIUS_M);
  lv_style_set_text_color(&focal_style_btn_primary, lv_color_hex(0xFFFFFF));

  lv_style_init(&focal_style_btn_secondary);
  lv_style_set_bg_opa(&focal_style_btn_secondary, LV_OPA_COVER);
  lv_style_set_radius(&focal_style_btn_secondary, FOCAL_RADIUS_M);
  lv_style_set_border_width(&focal_style_btn_secondary, 1);

  lv_style_init(&focal_style_btn_mint);
  lv_style_set_bg_color(&focal_style_btn_mint, FOCAL_COLOR_PASTEL_MINT);
  lv_style_set_bg_opa(&focal_style_btn_mint, LV_OPA_COVER);
  lv_style_set_radius(&focal_style_btn_mint, FOCAL_RADIUS_M);
  lv_style_set_text_color(&focal_style_btn_mint, lv_color_hex(0xFFFFFF));

  lv_style_init(&focal_style_btn_amber);
  lv_style_set_bg_color(&focal_style_btn_amber, FOCAL_COLOR_PASTEL_AMBER);
  lv_style_set_bg_opa(&focal_style_btn_amber, LV_OPA_COVER);
  lv_style_set_radius(&focal_style_btn_amber, FOCAL_RADIUS_M);
  lv_style_set_text_color(&focal_style_btn_amber, lv_color_hex(0xFFFFFF));

  lv_style_init(&focal_style_btn_danger);
  lv_style_set_bg_color(&focal_style_btn_danger, FOCAL_COLOR_PASTEL_RED);
  lv_style_set_bg_opa(&focal_style_btn_danger, LV_OPA_COVER);
  lv_style_set_radius(&focal_style_btn_danger, FOCAL_RADIUS_M);
  lv_style_set_text_color(&focal_style_btn_danger, lv_color_hex(0xFFFFFF));

  focal_theme_update_styles();
}

static void ui_refresh_theme_elements(void) {
  uint32_t text_primary = s_is_dark ? 0xF9FAFB : 0x111827;
  uint32_t text_secondary = s_is_dark ? 0x9CA3AF : 0x4B5563;
  uint32_t card_bg = s_is_dark ? 0x1A1D24 : 0xFFFFFF;

  // 1. Settings Sidebar Buttons & Icons
  for (int i = 0; i < 6; i++) {
    if (s_settings_menu_btns[i]) {
      lv_obj_t *ic = lv_obj_get_child(s_settings_menu_btns[i], 0);
      lv_obj_t *lbl = lv_obj_get_child(s_settings_menu_btns[i], 1);
      if (i == s_settings_tab) {
        lv_obj_set_style_bg_color(s_settings_menu_btns[i], focal_theme_get_accent_color(), 0);
        lv_obj_set_style_text_color(s_settings_menu_btns[i], lv_color_hex(0xFFFFFF), 0);
        if (ic) lv_obj_set_style_img_recolor(ic, lv_color_hex(0xFFFFFF), 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
      } else {
        lv_obj_set_style_bg_color(s_settings_menu_btns[i], lv_color_hex(card_bg), 0);
        lv_obj_set_style_text_color(s_settings_menu_btns[i], lv_color_hex(text_primary), 0);
        if (ic) lv_obj_set_style_img_recolor(ic, lv_color_hex(text_primary), 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(text_primary), 0);
      }
    }
  }

  // 2. Wi-Fi status label in Settings if disconnected
  if (s_lbl_wifi_detail && !wifi_manager_is_sta_connected()) {
    lv_obj_set_style_text_color(s_lbl_wifi_detail, lv_color_hex(text_secondary), 0);
  }

  // 3. Shapes Selection Buttons
  for (int i = 0; i < 4; i++) {
    if (s_shape_sel_btns[i]) {
      lv_obj_t *ic = lv_obj_get_child(s_shape_sel_btns[i], 0);
      if (i == s_shape_type) {
        lv_obj_set_style_bg_color(s_shape_sel_btns[i], focal_theme_get_accent_color(), 0);
        if (ic) lv_obj_set_style_img_recolor(ic, lv_color_hex(0xFFFFFF), 0);
      } else {
        lv_obj_set_style_bg_color(s_shape_sel_btns[i], lv_color_hex(card_bg), 0);
        if (ic) lv_obj_set_style_img_recolor(ic, lv_color_hex(text_primary), 0);
      }
    }
  }

  // 4. Bed Shape Preview accent color
  if (s_bed_shape_preview) {
    lv_obj_set_style_bg_color(s_bed_shape_preview, focal_theme_get_accent_color(), 0);
  }

  // 5. Bed Dimensions Screen Sim Box
  if (s_bed_dim_sim_box) {
    lv_obj_set_style_bg_color(s_bed_dim_sim_box, s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xE5E7EB), 0);
  }

  // 6. Settings Panels Text Colors
  for (int i = 0; i < 6; i++) {
    if (s_settings_panels[i]) {
      lv_obj_set_style_text_color(s_settings_panels[i], lv_color_hex(text_primary), 0);
    }
  }
}

const lv_img_dsc_t *focal_get_wifi_icon_by_rssi(int rssi) {
  if (rssi >= -60) {
    return &focal_icon_wifi;      // 3 arcs (full)
  } else if (rssi >= -75) {
    return &focal_icon_wifi_med;  // 2 arcs
  } else if (rssi >= -88) {
    return &focal_icon_wifi_low;  // 1 arc
  } else {
    return &focal_icon_wifi_dot;  // dot only
  }
}

void focal_theme_set_dark(bool is_dark) {
  s_is_dark = is_dark;
  focal_theme_update_styles();

  uint32_t bg_app = s_is_dark ? 0x0F1115 : 0xF5F6F8;
  uint32_t border_subtle = s_is_dark ? 0x2D333F : 0xD1D5DB;
  uint32_t text_primary = s_is_dark ? 0xF9FAFB : 0x111827;

  for (int i = 0; i < UI_SCREEN_MAX; i++) {
    if (s_hdrs[i]) {
      lv_obj_set_style_bg_color(s_hdrs[i], lv_color_hex(bg_app), 0);
      lv_obj_set_style_border_color(s_hdrs[i], lv_color_hex(border_subtle), 0);
    }
    if (s_hdr_titles[i]) {
      lv_obj_set_style_text_color(s_hdr_titles[i], lv_color_hex(text_primary),
                                  0);
    }
    if (s_hdr_pills[i]) {
      lv_obj_set_style_text_color(s_hdr_pills[i], lv_color_hex(text_primary),
                                  0);
    }
  }

  if (s_sd_preview) {
    gcode_preview_set_theme(s_sd_preview, s_is_dark);
    gcode_preview_render_sync(s_sd_preview, s_selected_file, NULL, NULL);
  }
  if (s_shapes_bed_preview) {
    gcode_preview_set_theme(s_shapes_bed_preview, s_is_dark);
  }
  if (s_cut_liquid_wave) {
    liquid_wave_progress_set_theme(s_cut_liquid_wave, s_is_dark);
  }

  for (int i = 0; i < 6; i++) {
    if (s_launcher_badges[i]) {
      lv_obj_set_style_bg_color(
          s_launcher_badges[i],
          s_is_dark ? lv_color_hex(0x262A35) : lv_color_hex(0xF0F2F5), 0);
      lv_obj_set_style_border_color(
          s_launcher_badges[i],
          s_is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xE5E7EB), 0);
    }
    if (s_launcher_titles[i]) {
      lv_obj_set_style_text_color(s_launcher_titles[i], lv_color_hex(text_primary),
                                  0);
    }
  }

  if (s_bed_obj) {
    lv_obj_set_style_bg_color(
        s_bed_obj, s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xE5E7EB),
        0);
  }
  if (s_wifi_list) {
    lv_obj_set_style_bg_color(
        s_wifi_list,
        s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xFFFFFF), 0);
  }
  if (s_wifi_modal) {
    lv_obj_set_style_bg_color(
        s_wifi_modal,
        s_is_dark ? lv_color_hex(0x0F1115) : lv_color_hex(0xF5F6F8), 0);
  }

  ui_refresh_theme_elements();
  ui_update_wifi_display();

  machine_settings_t cfg;
  settings_manager_get(&cfg);
  cfg.theme_mode = is_dark ? 0 : 1;
  settings_manager_save(&cfg);
}

bool focal_theme_is_dark(void) { return s_is_dark; }

void focal_theme_set_accent(uint8_t accent_idx) {
  if (accent_idx >= 5)
    accent_idx = 0;
  s_accent_idx = accent_idx;
  focal_theme_update_styles();
  ui_refresh_theme_elements();
  ui_update_wifi_display();

  machine_settings_t cfg;
  settings_manager_get(&cfg);
  cfg.accent_color_idx = accent_idx;
  settings_manager_save(&cfg);
}

uint8_t focal_theme_get_accent_idx(void) { return s_accent_idx; }

uint32_t focal_theme_get_accent_hex(void) {
  return s_is_dark ? s_accent_dark_hex[s_accent_idx]
                   : s_accent_light_hex[s_accent_idx];
}

lv_color_t focal_theme_get_accent_color(void) {
  return lv_color_hex(focal_theme_get_accent_hex());
}

// =============================================================================
// SCREEN 0: LAUNCHER GRID 3x2
// =============================================================================
static void on_launcher_card_click(lv_event_t *e) {
  ui_screen_id_t target = (ui_screen_id_t)(uintptr_t)lv_event_get_user_data(e);
  ui_switch_screen(target);
}

static void create_launcher_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  s_screens[UI_SCREEN_LAUNCHER] = scr;
  lv_obj_add_style(scr, &focal_style_screen, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_screen_header(UI_SCREEN_LAUNCHER, "Focal CNC Laser", false);

  typedef struct {
    const char *title;
    const lv_img_dsc_t *icon;
    uint32_t accent_hex;
    ui_screen_id_t target;
  } launcher_item_t;

  const launcher_item_t items[6] = {
      {"Control", &focal_icon_launcher_control, FOCAL_HEX_PASTEL_BLUE,
       UI_SCREEN_JOGGING},
      {"Archivos", &focal_icon_launcher_files, FOCAL_HEX_PASTEL_MINT,
       UI_SCREEN_SD_FILES},
      {"Monitor", &focal_icon_launcher_monitor, FOCAL_HEX_PASTEL_AMBER,
       UI_SCREEN_CUT_MONITOR},
      {"Calibracion", &focal_icon_launcher_calibration, 0xA78BFA,
       UI_SCREEN_CALIBRATION},
      {"Corte Rapido", &focal_icon_launcher_shapes,
       FOCAL_HEX_PASTEL_BLUE, UI_SCREEN_SHAPES},
      {"Ajustes", &focal_icon_launcher_settings,
       FOCAL_HEX_PASTEL_MINT, UI_SCREEN_SETTINGS}};

  const int card_w = 146;
  const int card_h = 124;
  const int start_x = 10;
  const int start_y = 50;
  const int gap_x = 10;
  const int gap_y = 10;

  for (int i = 0; i < 6; i++) {
    int col = i % 3;
    int row = i / 3;
    int x = start_x + col * (card_w + gap_x);
    int y = start_y + row * (card_h + gap_y);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, card_w, card_h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_style(btn, &focal_style_btn_secondary, 0);
    lv_obj_set_style_radius(btn, FOCAL_RADIUS_L, 0);
    lv_obj_set_style_pad_all(btn, 6, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(btn, 6);
    lv_obj_add_event_cb(btn, on_launcher_card_click, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)items[i].target);

    // Badge Container (Size 54x54 px for 35px 25% larger vector icons)
    lv_obj_t *badge = lv_obj_create(btn);
    lv_obj_set_size(badge, 54, 54);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(
        badge, s_is_dark ? lv_color_hex(0x262A35) : lv_color_hex(0xF0F2F5), 0);
    lv_obj_set_style_radius(badge, FOCAL_RADIUS_M, 0);
    lv_obj_set_style_border_color(
        badge, s_is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xE5E7EB), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    // CRITICAL TOUCH FIX: Allow touch events on the badge to bubble or be transparent to the parent button,
    // plus directly binding on_launcher_card_click to badge so touching the badge ALWAYS works!
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(badge, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(badge, on_launcher_card_click, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)items[i].target);
    s_launcher_badges[i] = badge;

    lv_obj_t *ic = focal_create_icon(badge, items[i].icon, items[i].accent_hex);
    lv_obj_center(ic);
    lv_obj_clear_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ic, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Title label without detail texts underneath
    lv_obj_t *lbl_t = lv_label_create(btn);
    lv_label_set_text(lbl_t, items[i].title);
    lv_obj_set_style_text_font(lbl_t, FOCAL_FONT, 0);
    lv_obj_set_style_text_color(
        lbl_t, s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
    lv_obj_align(lbl_t, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_clear_flag(lbl_t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(lbl_t, LV_OBJ_FLAG_EVENT_BUBBLE);
    s_launcher_titles[i] = lbl_t;
    s_launcher_title_lbls[i] = lbl_t;
  }
}

// =============================================================================
// SCREEN 1: CONTROL DE MAQUINA (JOGGING & DRO)
// =============================================================================
static float s_jog_step = 10.0f;
static float s_mach_x = 0.0f;
static float s_mach_y = 0.0f;
static float s_vhome_x = 0.0f;
static float s_vhome_y = 0.0f;
static float s_sim_x = 0.0f;
static float s_sim_y = 0.0f;
static lv_obj_t *s_dro_x = NULL;
static lv_obj_t *s_dro_y = NULL;
static lv_obj_t *s_step_btns[4] = {0};
static lv_obj_t *s_jog_lock_modal = NULL;

// Reusable Machine Bed Component Configuration & State
typedef struct {
  float bed_width_mm;   // Work envelope width (X), e.g. 160.0f
  float bed_height_mm;  // Work envelope height (Y), e.g. 200.0f
  bool invert_x;        // Invert X axis (false: 0 is left)
  bool invert_y;        // Invert Y axis (true: 0 is front / bottom)
  bool swap_xy;         // Swap axes for orientation changes
} focal_bed_config_t;

typedef struct {
  lv_obj_t *container;       // Canvas background container
  lv_obj_t *grid_h;          // Subtle crosshair horizontal line
  lv_obj_t *grid_v;          // Subtle crosshair vertical line
  lv_obj_t *reticle_ring;    // Laser reticle outer ring (Mint/Blue)
  lv_obj_t *reticle_dot;     // Laser reticle central focal point
  lv_obj_t *lbl_origin;      // (0,0) Origin tag
  lv_obj_t *lbl_bounds;      // Machine dimensions tag (e.g. 160x200)
  focal_bed_config_t config; // Parametric machine geometry
  int16_t w;                 // Width in pixels
  int16_t h;                 // Height in pixels
} focal_bed_view_t;

static focal_bed_view_t s_jog_bed_instance;
static focal_bed_view_t *s_jog_bed = NULL;

static void focal_bed_calc_screen_coords(float x_mm, float y_mm, int w_mm, int h_mm, int rot_deg, float *out_nx, float *out_ny) {
  float nx = (w_mm > 0) ? (x_mm / (float)w_mm) : 0.0f;
  float ny = (h_mm > 0) ? (y_mm / (float)h_mm) : 0.0f;
  if (nx < 0.0f) nx = 0.0f; else if (nx > 1.0f) nx = 1.0f;
  if (ny < 0.0f) ny = 0.0f; else if (ny > 1.0f) ny = 1.0f;

  switch (rot_deg) {
    case 90:
      *out_nx = 1.0f - ny;
      *out_ny = 1.0f - nx;
      break;
    case 180:
      *out_nx = 1.0f - nx;
      *out_ny = ny;
      break;
    case 270:
      *out_nx = ny;
      *out_ny = nx;
      break;
    default: // 0
      *out_nx = nx;
      *out_ny = 1.0f - ny;
      break;
  }
}

static void focal_bed_view_set_pos(focal_bed_view_t *bed, float x_mm, float y_mm);

static focal_bed_view_t *focal_bed_view_create(lv_obj_t *parent, int16_t w,
                                               int16_t h,
                                               const focal_bed_config_t *cfg) {
  focal_bed_view_t *b = &s_jog_bed_instance;
  memset(b, 0, sizeof(focal_bed_view_t));
  if (cfg) {
    b->config = *cfg;
  } else {
    b->config.bed_width_mm = 160.0f;
    b->config.bed_height_mm = 200.0f;
    b->config.invert_x = false;
    b->config.invert_y = true;
    b->config.swap_xy = false;
  }
  b->w = w;
  b->h = h;

  // Bed Canvas Surface
  b->container = lv_obj_create(parent);
  lv_obj_set_size(b->container, w, h);
  lv_obj_set_style_bg_color(
      b->container, s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xFAFAFC),
      0);
  lv_obj_set_style_bg_opa(b->container, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(
      b->container, s_is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xD1D5DB),
      0);
  lv_obj_set_style_border_width(b->container, 1, 0);
  lv_obj_set_style_radius(b->container, FOCAL_RADIUS_S, 0);
  lv_obj_set_style_pad_all(b->container, 0, 0);
  lv_obj_clear_flag(b->container, LV_OBJ_FLAG_SCROLLABLE);

  // Subtle crosshair grid lines at 50%
  b->grid_h = lv_obj_create(b->container);
  lv_obj_set_size(b->grid_h, w - 12, 1);
  lv_obj_align(b->grid_h, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(
      b->grid_h, s_is_dark ? lv_color_hex(0x222630) : lv_color_hex(0xEDF0F4), 0);
  lv_obj_set_style_border_width(b->grid_h, 0, 0);
  lv_obj_clear_flag(b->grid_h, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  b->grid_v = lv_obj_create(b->container);
  lv_obj_set_size(b->grid_v, 1, h - 12);
  lv_obj_align(b->grid_v, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(
      b->grid_v, s_is_dark ? lv_color_hex(0x222630) : lv_color_hex(0xEDF0F4), 0);
  lv_obj_set_style_border_width(b->grid_v, 0, 0);
  lv_obj_clear_flag(b->grid_v, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  // Dimension indicator in top-right
  b->lbl_bounds = lv_label_create(b->container);
  char bnd[32];
  snprintf(bnd, sizeof(bnd), "%dx%d", s_bed_w_mm, s_bed_h_mm);
  lv_label_set_text(b->lbl_bounds, bnd);
  lv_obj_set_style_text_color(
      b->lbl_bounds, s_is_dark ? lv_color_hex(0x4B5563) : lv_color_hex(0x9CA3AF),
      0);
  lv_obj_set_style_text_font(b->lbl_bounds, FOCAL_FONT, 0);
  lv_obj_align(b->lbl_bounds, LV_ALIGN_TOP_RIGHT, -6, 4);

  // Laser Head Reticle: Outer Ring (16x16 circle)
  b->reticle_ring = lv_obj_create(b->container);
  lv_obj_set_size(b->reticle_ring, 16, 16);
  lv_obj_set_style_radius(b->reticle_ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b->reticle_ring, LV_OPA_20, 0);
  lv_obj_set_style_bg_color(b->reticle_ring, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_set_style_border_color(b->reticle_ring, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_set_style_border_width(b->reticle_ring, 2, 0);
  lv_obj_set_style_pad_all(b->reticle_ring, 0, 0);
  lv_obj_clear_flag(b->reticle_ring,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  // Laser Head Reticle: Center Core Dot (4x4)
  b->reticle_dot = lv_obj_create(b->reticle_ring);
  lv_obj_set_size(b->reticle_dot, 4, 4);
  lv_obj_set_style_radius(b->reticle_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b->reticle_dot, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_set_style_border_width(b->reticle_dot, 0, 0);
  lv_obj_center(b->reticle_dot);
  lv_obj_clear_flag(b->reticle_dot,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  focal_bed_view_set_pos(b, 0.0f, 0.0f);
  return b;
}

static void focal_bed_view_set_pos(focal_bed_view_t *bed, float x_mm,
                                   float y_mm) {
  if (!bed || !bed->container || !bed->reticle_ring)
    return;

  float nx, ny;
  focal_bed_calc_screen_coords(x_mm, y_mm, s_bed_w_mm, s_bed_h_mm, s_bed_rot, &nx, &ny);

  int ring_size = 16;
  int margin = 4;
  int px_x = margin + (int)(nx * (bed->w - ring_size - 2 * margin));
  int px_y = margin + (int)(ny * (bed->h - ring_size - 2 * margin));

  lv_obj_set_pos(bed->reticle_ring, px_x, px_y);
}

static void update_jog_bed_geometry(void) {
  if (!s_jog_bed || !s_jog_bed->container) return;

  int eff_w = (s_bed_rot == 90 || s_bed_rot == 270) ? s_bed_h_mm : s_bed_w_mm;
  int eff_h = (s_bed_rot == 90 || s_bed_rot == 270) ? s_bed_w_mm : s_bed_h_mm;
  if (eff_w < 50) eff_w = 50;
  if (eff_h < 50) eff_h = 50;

  float scale = fminf(170.0f / (float)eff_w, 190.0f / (float)eff_h);
  int bw = (int)(eff_w * scale);
  int bh = (int)(eff_h * scale);
  if (bw < 50) bw = 50;
  if (bh < 50) bh = 50;

  s_jog_bed->w = bw;
  s_jog_bed->h = bh;
  s_jog_bed->config.bed_width_mm = (float)s_bed_w_mm;
  s_jog_bed->config.bed_height_mm = (float)s_bed_h_mm;

  lv_obj_set_size(s_jog_bed->container, bw, bh);
  lv_obj_set_pos(s_jog_bed->container, (210 - bw) / 2, 10 + (190 - bh) / 2);

  if (s_jog_bed->grid_h) {
    lv_obj_set_size(s_jog_bed->grid_h, bw - 12, 1);
    lv_obj_align(s_jog_bed->grid_h, LV_ALIGN_CENTER, 0, 0);
  }
  if (s_jog_bed->grid_v) {
    lv_obj_set_size(s_jog_bed->grid_v, 1, bh - 12);
    lv_obj_align(s_jog_bed->grid_v, LV_ALIGN_CENTER, 0, 0);
  }
  if (s_jog_bed->lbl_bounds) {
    char bnd[32];
    snprintf(bnd, sizeof(bnd), "%dx%d", s_bed_w_mm, s_bed_h_mm);
    lv_label_set_text(s_jog_bed->lbl_bounds, bnd);
  }
  focal_bed_view_set_pos(s_jog_bed, s_mach_x, s_mach_y);
}

static void update_calibration_bed_geometry(void) {
  if (!s_calib_bed_box) return;

  int eff_w = (s_bed_rot == 90 || s_bed_rot == 270) ? s_bed_h_mm : s_bed_w_mm;
  int eff_h = (s_bed_rot == 90 || s_bed_rot == 270) ? s_bed_w_mm : s_bed_h_mm;
  if (eff_w < 50) eff_w = 50;
  if (eff_h < 50) eff_h = 50;

  float scale = fminf(150.0f / (float)eff_w, 175.0f / (float)eff_h);
  int bw = (int)(eff_w * scale);
  int bh = (int)(eff_h * scale);
  if (bw < 50) bw = 50;
  if (bh < 50) bh = 50;

  lv_obj_set_size(s_calib_bed_box, bw, bh);
  lv_obj_align(s_calib_bed_box, LV_ALIGN_TOP_MID, 0, 38 + (175 - bh) / 2);

  if (s_calib_lbl_bed_dim) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Cama %dx%d mm", s_bed_w_mm, s_bed_h_mm);
    lv_label_set_text(s_calib_lbl_bed_dim, buf);
  }

  // 5 probe points in physical mm coordinates: T1, T2, T3, T4, Center
  float pts_mm[5][2] = {
    {10.0f, 10.0f},
    {(float)s_bed_w_mm - 10.0f, 10.0f},
    {(float)s_bed_w_mm - 10.0f, (float)s_bed_h_mm - 10.0f},
    {10.0f, (float)s_bed_h_mm - 10.0f},
    {(float)s_bed_w_mm * 0.5f, (float)s_bed_h_mm * 0.5f}
  };

  int m = 10;
  for (int i = 0; i < 5; i++) {
    if (s_calib_pts[i]) {
      float nx, ny;
      focal_bed_calc_screen_coords(pts_mm[i][0], pts_mm[i][1], s_bed_w_mm, s_bed_h_mm, s_bed_rot, &nx, &ny);
      int px = m + (int)(nx * (bw - 32 - 2 * m));
      int py = m + (int)(ny * (bh - 32 - 2 * m));
      lv_obj_set_pos(s_calib_pts[i], px, py);
    }
  }

  if (s_calib_laser_dot && s_calib_step >= 0 && s_calib_step < 5 && s_calib_pts[s_calib_step]) {
    lv_obj_align_to(s_calib_laser_dot, s_calib_pts[s_calib_step], LV_ALIGN_CENTER, 0, 0);
  }
}

static void update_shapes_bed_geometry(void) {
  if (!s_bed_obj) return;

  int eff_w = (s_bed_rot == 90 || s_bed_rot == 270) ? s_bed_h_mm : s_bed_w_mm;
  int eff_h = (s_bed_rot == 90 || s_bed_rot == 270) ? s_bed_w_mm : s_bed_h_mm;
  if (eff_w < 50) eff_w = 50;
  if (eff_h < 50) eff_h = 50;

  float scale = fminf(170.0f / (float)eff_w, 146.0f / (float)eff_h);
  int bw = (int)(eff_w * scale);
  int bh = (int)(eff_h * scale);
  if (bw < 40) bw = 40;
  if (bh < 40) bh = 40;

  lv_obj_set_size(s_bed_obj, bw, bh);
  lv_obj_align(s_bed_obj, LV_ALIGN_TOP_MID, 0, 4 + (146 - bh) / 2);

  update_bed_shape_position();
}

static void update_dro_display(void) {
  char buf[24];
  float wx = s_mach_x - s_vhome_x;
  float wy = s_mach_y - s_vhome_y;
  s_sim_x = wx;
  s_sim_y = wy;
  if (s_dro_x) {
    snprintf(buf, sizeof(buf), "X: %5.1f", wx);
    lv_label_set_text(s_dro_x, buf);
  }
  if (s_dro_y) {
    snprintf(buf, sizeof(buf), "Y: %5.1f", wy);
    lv_label_set_text(s_dro_y, buf);
  }
  if (s_jog_bed) {
    focal_bed_view_set_pos(s_jog_bed, s_mach_x, s_mach_y);
  }
}

void ui_set_bed_dimensions(int w_mm, int h_mm, int rotation_deg) {
  if (w_mm < 50) w_mm = 50;
  if (w_mm > 1200) w_mm = 1200;
  if (h_mm < 50) h_mm = 50;
  if (h_mm > 1200) h_mm = 1200;
  rotation_deg = (rotation_deg % 360 + 360) % 360;

  s_bed_w_mm = w_mm;
  s_bed_h_mm = h_mm;
  s_bed_rot = rotation_deg;

  machine_settings_t cfg;
  settings_manager_get(&cfg);
  cfg.travel_max_x = (float)w_mm;
  cfg.travel_max_y = (float)h_mm;
  cfg.bed_rotation = (int16_t)rotation_deg;
  settings_manager_save(&cfg);

  char cmd[64], resp[64];
  snprintf(cmd, sizeof(cmd), "$130=%d", w_mm);
  gcode_parser_execute_line(cmd, resp, sizeof(resp));
  snprintf(cmd, sizeof(cmd), "$131=%d", h_mm);
  gcode_parser_execute_line(cmd, resp, sizeof(resp));

  update_jog_bed_geometry();
  update_calibration_bed_geometry();
  update_shapes_bed_geometry();

  if (s_lbl_kin_bed_info) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Cama: %d x %d mm  |  Rot: %d°", s_bed_w_mm, s_bed_h_mm, s_bed_rot);
    lv_label_set_text(s_lbl_kin_bed_info, buf);
  }
}

void ui_get_bed_dimensions(int *w_mm, int *h_mm, int *rotation_deg) {
  if (w_mm) *w_mm = s_bed_w_mm;
  if (h_mm) *h_mm = s_bed_h_mm;
  if (rotation_deg) *rotation_deg = s_bed_rot;
}

static void on_jog_step_select_cb(lv_event_t *e) {
  int idx = (int)(uintptr_t)lv_event_get_user_data(e);
  const float steps[4] = {0.1f, 1.0f, 10.0f, 50.0f};
  s_jog_step = steps[idx];

  for (int i = 0; i < 4; i++) {
    if (s_step_btns[i]) {
      if (i == idx) {
        lv_obj_set_style_bg_color(s_step_btns[i],
                                  focal_theme_get_accent_color(), 0);
        lv_obj_set_style_text_color(s_step_btns[i], lv_color_hex(0xFFFFFF), 0);
      } else {
        lv_obj_set_style_bg_color(
            s_step_btns[i],
            s_is_dark ? lv_color_hex(0x1A1D24) : lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(
            s_step_btns[i],
            s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
      }
    }
  }
}

static void on_jog_lock_action_cb(lv_event_t *e) {
  const char *target = (const char *)lv_event_get_user_data(e);
  if (strcmp(target, "MONITOR") == 0) {
    ui_switch_screen(UI_SCREEN_CUT_MONITOR);
  } else {
    ui_switch_screen(UI_SCREEN_LAUNCHER);
  }
}

static void on_jog_btn_cb(lv_event_t *e) {
  // Safety guard: no jog commands permitted while a job is cutting
  if (s_is_cutting) {
    if (s_jog_lock_modal) {
      lv_obj_clear_flag(s_jog_lock_modal, LV_OBJ_FLAG_HIDDEN);
    }
    return;
  }

  const char *dir = (const char *)lv_event_get_user_data(e);
  char cmd[64];
  char resp[32];

  if (strcmp(dir, "ZERO") == 0) {
    // Relative virtual home: current head position becomes work coordinate (0,0)
    s_vhome_x = s_mach_x;
    s_vhome_y = s_mach_y;
    strcpy(cmd, "G92 X0 Y0");
    gcode_parser_execute_line(cmd, resp, sizeof(resp));
  } else if (strcmp(dir, "HOME_REL") == 0 || strcmp(dir, "HOME") == 0) {
    // Return to relative/work (0,0) (virtual home position)
    s_mach_x = s_vhome_x;
    s_mach_y = s_vhome_y;
    snprintf(cmd, sizeof(cmd), "G90 G0 X%.2f Y%.2f F3000", s_mach_x, s_mach_y);
    gcode_parser_execute_line(cmd, resp, sizeof(resp));
  } else if (strcmp(dir, "HOME_ABS") == 0) {
    // Machine home: physical movement to absolute origin (0,0)
    s_mach_x = 0.0f;
    s_mach_y = 0.0f;
    s_vhome_x = 0.0f;
    s_vhome_y = 0.0f;
    strcpy(cmd, "$H");
    gcode_parser_execute_line(cmd, resp, sizeof(resp));
  } else {
    // Directional jog: transformed by bed rotation so visual UP is always visual UP
    float dx = 0.0f;
    float dy = 0.0f;
    if (strcmp(dir, "Y+") == 0 || strcmp(dir, "UP") == 0) {
      switch (s_bed_rot) {
        case 90:  dx =  s_jog_step; break;
        case 180: dy = -s_jog_step; break;
        case 270: dx = -s_jog_step; break;
        default:  dy =  s_jog_step; break; // 0°
      }
    } else if (strcmp(dir, "Y-") == 0 || strcmp(dir, "DOWN") == 0) {
      switch (s_bed_rot) {
        case 90:  dx = -s_jog_step; break;
        case 180: dy =  s_jog_step; break;
        case 270: dx =  s_jog_step; break;
        default:  dy = -s_jog_step; break; // 0°
      }
    } else if (strcmp(dir, "X-") == 0 || strcmp(dir, "LEFT") == 0) {
      switch (s_bed_rot) {
        case 90:  dy =  s_jog_step; break;
        case 180: dx =  s_jog_step; break;
        case 270: dy = -s_jog_step; break;
        default:  dx = -s_jog_step; break; // 0°
      }
    } else if (strcmp(dir, "X+") == 0 || strcmp(dir, "RIGHT") == 0) {
      switch (s_bed_rot) {
        case 90:  dy = -s_jog_step; break;
        case 180: dx = -s_jog_step; break;
        case 270: dy =  s_jog_step; break;
        default:  dx =  s_jog_step; break; // 0°
      }
    }

    if (dx != 0.0f) {
      s_mach_x += dx;
      if (s_mach_x < 0.0f) s_mach_x = 0.0f;
      if (s_mach_x > (float)s_bed_w_mm) s_mach_x = (float)s_bed_w_mm;
      snprintf(cmd, sizeof(cmd), "$J=G91 G21 X%.2f F2000", dx);
      gcode_parser_execute_line(cmd, resp, sizeof(resp));
    }
    if (dy != 0.0f) {
      s_mach_y += dy;
      if (s_mach_y < 0.0f) s_mach_y = 0.0f;
      if (s_mach_y > (float)s_bed_h_mm) s_mach_y = (float)s_bed_h_mm;
      snprintf(cmd, sizeof(cmd), "$J=G91 G21 Y%.2f F2000", dy);
      gcode_parser_execute_line(cmd, resp, sizeof(resp));
    }
  }

  update_dro_display();
}

static void create_jogging_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  s_screens[UI_SCREEN_JOGGING] = scr;
  lv_obj_add_style(scr, &focal_style_screen, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_screen_header(UI_SCREEN_JOGGING, "Control de Ejes", true);

  // Left Panel: Interactive Laser Bed Simulation, Fijar Home, and DRO
  lv_obj_t *card_dro = lv_obj_create(scr);
  lv_obj_set_size(card_dro, 210, 260);
  lv_obj_set_pos(card_dro, 10, 48);
  lv_obj_add_style(card_dro, &focal_style_card, 0);
  lv_obj_set_style_pad_all(card_dro, 0, 0);
  lv_obj_clear_flag(card_dro, LV_OBJ_FLAG_SCROLLABLE);

  // 2D Machine Bed Simulation (dynamic aspect ratio & orientation)
  s_jog_bed = focal_bed_view_create(card_dro, 152, 190, NULL);
  update_jog_bed_geometry();

  // Bottom Area: Compact DRO and Fijar Home centered horizontally together
  // Group spans X=18..192 (18px left margin, 18px right margin in 210px card)
  s_dro_x = lv_label_create(card_dro);
  lv_label_set_text(s_dro_x, "X: 0.0");
  lv_obj_set_style_text_color(s_dro_x, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_style_text_font(s_dro_x, FOCAL_FONT, 0);
  lv_obj_set_pos(s_dro_x, 18, 214);

  s_dro_y = lv_label_create(card_dro);
  lv_label_set_text(s_dro_y, "Y: 0.0");
  lv_obj_set_style_text_color(s_dro_y, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_set_style_text_font(s_dro_y, FOCAL_FONT, 0);
  lv_obj_set_pos(s_dro_y, 18, 230);

  // Compact "Fijar Home" Pill (aligned vertically with X/Y labels, height 32px)
  lv_obj_t *btn_zero =
      focal_create_btn(card_dro, 114, 32, &focal_icon_check, "Fijar Home",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_MINT);
  lv_obj_set_pos(btn_zero, 78, 214);
  lv_obj_set_style_radius(btn_zero, FOCAL_RADIUS_S, 0);
  lv_obj_set_ext_click_area(btn_zero, 6);
  lv_obj_add_event_cb(btn_zero, on_jog_btn_cb, LV_EVENT_CLICKED,
                      (void *)"ZERO");
  s_btn_zero_lbl = lv_obj_get_child(btn_zero, 1);

  // Right Panel: Step selector and Pad
  lv_obj_t *card_jog = lv_obj_create(scr);
  lv_obj_set_size(card_jog, 240, 260);
  lv_obj_set_pos(card_jog, 230, 48);
  lv_obj_add_style(card_jog, &focal_style_card, 0);
  lv_obj_set_style_pad_all(card_jog, 0, 0);
  lv_obj_clear_flag(card_jog, LV_OBJ_FLAG_SCROLLABLE);

  const char *step_names[4] = {"0.1", "1.0", "10", "50"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *btn_s = lv_btn_create(card_jog);
    lv_obj_set_size(btn_s, 50, 32);
    lv_obj_set_pos(btn_s, 11 + i * 56, 10);
    lv_obj_add_style(btn_s, &focal_style_btn_secondary, 0);
    lv_obj_set_style_radius(btn_s, FOCAL_RADIUS_S, 0);
    s_step_btns[i] = btn_s;

    lv_obj_t *lbl = lv_label_create(btn_s);
    lv_label_set_text(lbl, step_names[i]);
    lv_obj_center(lbl);

    if (i == 2) {
      lv_obj_set_style_bg_color(btn_s, focal_theme_get_accent_color(), 0);
      lv_obj_set_style_text_color(btn_s, lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_add_event_cb(btn_s, on_jog_step_select_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
  }

  // Directional Pad - Uniform 50x50px geometry, perfectly centered, equal 8px spacing
  lv_obj_t *btn_up = lv_btn_create(card_jog);
  lv_obj_set_size(btn_up, 50, 50);
  lv_obj_align(btn_up, LV_ALIGN_CENTER, 0, -40);
  lv_obj_add_style(btn_up, &focal_style_btn_secondary, 0);
  lv_obj_set_style_radius(btn_up, FOCAL_RADIUS_M, 0);
  lv_obj_set_style_pad_all(btn_up, 0, 0);
  lv_obj_set_ext_click_area(btn_up, 6);
  lv_obj_t *ic_up =
      focal_create_icon(btn_up, &focal_icon_jog_up, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_center(ic_up);
  lv_obj_add_event_cb(btn_up, on_jog_btn_cb, LV_EVENT_CLICKED, (void *)"Y+");

  lv_obj_t *btn_dn = lv_btn_create(card_jog);
  lv_obj_set_size(btn_dn, 50, 50);
  lv_obj_align(btn_dn, LV_ALIGN_CENTER, 0, 76);
  lv_obj_add_style(btn_dn, &focal_style_btn_secondary, 0);
  lv_obj_set_style_radius(btn_dn, FOCAL_RADIUS_M, 0);
  lv_obj_set_style_pad_all(btn_dn, 0, 0);
  lv_obj_set_ext_click_area(btn_dn, 6);
  lv_obj_t *ic_dn =
      focal_create_icon(btn_dn, &focal_icon_jog_down, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_center(ic_dn);
  lv_obj_add_event_cb(btn_dn, on_jog_btn_cb, LV_EVENT_CLICKED, (void *)"Y-");

  lv_obj_t *btn_lt = lv_btn_create(card_jog);
  lv_obj_set_size(btn_lt, 50, 50);
  lv_obj_align(btn_lt, LV_ALIGN_CENTER, -58, 18);
  lv_obj_add_style(btn_lt, &focal_style_btn_secondary, 0);
  lv_obj_set_style_radius(btn_lt, FOCAL_RADIUS_M, 0);
  lv_obj_set_style_pad_all(btn_lt, 0, 0);
  lv_obj_set_ext_click_area(btn_lt, 6);
  lv_obj_t *ic_lt =
      focal_create_icon(btn_lt, &focal_icon_jog_left, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_center(ic_lt);
  lv_obj_add_event_cb(btn_lt, on_jog_btn_cb, LV_EVENT_CLICKED, (void *)"X-");

  lv_obj_t *btn_rt = lv_btn_create(card_jog);
  lv_obj_set_size(btn_rt, 50, 50);
  lv_obj_align(btn_rt, LV_ALIGN_CENTER, 58, 18);
  lv_obj_add_style(btn_rt, &focal_style_btn_secondary, 0);
  lv_obj_set_style_radius(btn_rt, FOCAL_RADIUS_M, 0);
  lv_obj_set_style_pad_all(btn_rt, 0, 0);
  lv_obj_set_ext_click_area(btn_rt, 6);
  lv_obj_t *ic_rt =
      focal_create_icon(btn_rt, &focal_icon_jog_right, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_center(ic_rt);
  lv_obj_add_event_cb(btn_rt, on_jog_btn_cb, LV_EVENT_CLICKED, (void *)"X+");

  // Center Button: Relative Home Button (returns to relative (0,0) work home)
  lv_obj_t *btn_hm = lv_btn_create(card_jog);
  lv_obj_set_size(btn_hm, 50, 50);
  lv_obj_align(btn_hm, LV_ALIGN_CENTER, 0, 18);
  lv_obj_add_style(btn_hm, &focal_style_btn_secondary, 0);
  lv_obj_set_style_radius(btn_hm, FOCAL_RADIUS_M, 0);
  lv_obj_set_style_pad_all(btn_hm, 0, 0);
  lv_obj_set_ext_click_area(btn_hm, 6);
  lv_obj_t *ic_hm =
      focal_create_icon(btn_hm, &focal_icon_home, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_center(ic_hm);
  lv_obj_add_event_cb(btn_hm, on_jog_btn_cb, LV_EVENT_CLICKED, (void *)"HOME_REL");

  // Absolute Real Home Button: Returns to machine physical (0,0) with distinctive datum house icon
  lv_obj_t *btn_hm_abs = lv_btn_create(card_jog);
  lv_obj_set_size(btn_hm_abs, 46, 46);
  lv_obj_set_pos(btn_hm_abs, 182, 202);
  lv_obj_add_style(btn_hm_abs, &focal_style_btn_secondary, 0);
  lv_obj_set_style_radius(btn_hm_abs, FOCAL_RADIUS_M, 0);
  lv_obj_set_style_pad_all(btn_hm_abs, 0, 0);
  lv_obj_set_ext_click_area(btn_hm_abs, 6);
  lv_obj_t *ic_hm_abs =
      focal_create_icon(btn_hm_abs, &focal_icon_home_abs, FOCAL_HEX_PASTEL_MINT);
  lv_obj_center(ic_hm_abs);
  lv_obj_add_event_cb(btn_hm_abs, on_jog_btn_cb, LV_EVENT_CLICKED, (void *)"HOME_ABS");

  // Safety Interlock Modal Overlay (dimmed background + warning card when cutting is active)
  s_jog_lock_modal = lv_obj_create(scr);
  lv_obj_set_size(s_jog_lock_modal, 480, 320);
  lv_obj_set_pos(s_jog_lock_modal, 0, 0);
  lv_obj_set_style_bg_color(s_jog_lock_modal, lv_color_hex(0x0A0C10), 0);
  lv_obj_set_style_bg_opa(s_jog_lock_modal, LV_OPA_80, 0);
  lv_obj_set_style_border_width(s_jog_lock_modal, 0, 0);
  lv_obj_set_style_pad_all(s_jog_lock_modal, 0, 0);
  lv_obj_clear_flag(s_jog_lock_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_jog_lock_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *lock_card = lv_obj_create(s_jog_lock_modal);
  lv_obj_set_size(lock_card, 360, 180);
  lv_obj_center(lock_card);
  lv_obj_add_style(lock_card, &focal_style_card, 0);
  lv_obj_set_style_border_color(lock_card, FOCAL_COLOR_PASTEL_AMBER, 0);
  lv_obj_set_style_border_width(lock_card, 2, 0);
  lv_obj_clear_flag(lock_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *ic_warn =
      focal_create_icon(lock_card, &focal_icon_estop, FOCAL_HEX_PASTEL_AMBER);
  lv_obj_align(ic_warn, LV_ALIGN_TOP_MID, 0, 6);

  lv_obj_t *lbl_lock_title = lv_label_create(lock_card);
  lv_label_set_text(lbl_lock_title, "CONTROL MANUAL BLOQUEADO");
  lv_obj_set_style_text_color(lbl_lock_title, FOCAL_COLOR_PASTEL_AMBER, 0);
  lv_obj_set_style_text_font(lbl_lock_title, FOCAL_FONT, 0);
  lv_obj_align(lbl_lock_title, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t *lbl_lock_desc = lv_label_create(lock_card);
  lv_label_set_text(lbl_lock_desc,
                    "El movimiento de ejes esta deshabilitado\nmientras la maquina ejecuta un corte.");
  lv_obj_set_style_text_align(lbl_lock_desc, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(
      lbl_lock_desc,
      s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x4B5563), 0);
  lv_obj_align(lbl_lock_desc, LV_ALIGN_TOP_MID, 0, 58);

  lv_obj_t *btn_to_mon = focal_create_btn(
      lock_card, 144, 36, &focal_icon_monitor, "Ir al Monitor",
      &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_align(btn_to_mon, LV_ALIGN_BOTTOM_LEFT, 16, -10);
  lv_obj_set_ext_click_area(btn_to_mon, 6);
  lv_obj_add_event_cb(btn_to_mon, on_jog_lock_action_cb, LV_EVENT_CLICKED,
                      (void *)"MONITOR");

  lv_obj_t *btn_to_menu = focal_create_btn(
      lock_card, 144, 36, &focal_icon_back, "Menu",
      &focal_style_btn_secondary, 0);
  lv_obj_align(btn_to_menu, LV_ALIGN_BOTTOM_RIGHT, -16, -10);
  lv_obj_set_ext_click_area(btn_to_menu, 6);
  lv_obj_add_event_cb(btn_to_menu, on_jog_lock_action_cb, LV_EVENT_CLICKED,
                      (void *)"MENU");

  s_jog_lock_title_lbl = lbl_lock_title;
  s_jog_lock_desc_lbl = lbl_lock_desc;
  s_jog_lock_gomon_lbl = lv_obj_get_child(btn_to_mon, 1);
  s_jog_lock_menu_lbl = lv_obj_get_child(btn_to_menu, 1);

  update_dro_display();
}

// =============================================================================
// SCREEN 2: GESTOR DE ARCHIVOS EN MICROSD & FAVORITOS FIFO
// =============================================================================
#define MAX_FAVORITE_FILES 2
static char s_favorite_files[MAX_FAVORITE_FILES][64] = {"", ""};
static int s_num_favorites = 0;
static lv_obj_t *s_fav_btns[MAX_FAVORITE_FILES] = {NULL, NULL};
static lv_obj_t *s_fav_labels[MAX_FAVORITE_FILES] = {NULL, NULL};
static int s_active_favorite_idx = -1;
static void update_shapes_favorite_buttons(void);

#define NUM_SD_SAMPLE_FILES 8
static char s_sd_files[NUM_SD_SAMPLE_FILES][64] = {
    "prueba_mario.gcode",
    "corte_circulo_50mm.gcode",
    "calibracion_cama.gcode",
    "caja_ensamble.gcode",
    "soporte_laser.gcode",
    "engranaje_24d.gcode",
    "plantilla_corte.gcode",
    "panel_frontal.gcode"
};
static bool s_sd_file_deleted[NUM_SD_SAMPLE_FILES] = {false};
static lv_obj_t *s_sd_file_items[NUM_SD_SAMPLE_FILES] = {NULL};

static lv_timer_t *s_sd_lp_timer = NULL;
static bool s_sd_lp_fired = false;
static int s_sd_lp_file_idx = -1;

// Modals on lv_layer_top()
static lv_obj_t *s_sd_options_modal = NULL;
static lv_obj_t *s_sd_options_file_lbl = NULL;
static lv_obj_t *s_sd_del_modal = NULL;
static lv_obj_t *s_sd_del_msg_lbl = NULL;
static char s_sd_modal_target_file[64] = "";
static int s_sd_modal_target_idx = -1;
static lv_obj_t *s_lbl_file_name = NULL;
static lv_obj_t *s_lbl_file_info = NULL;
static void on_preview_ready_cb(gcode_preview_view_t *view,
                                const gcode_preview_meta_t *meta,
                                void *user_data);

void ui_add_favorite_file(const char *filename) {
  if (!filename || strlen(filename) == 0) return;

  // If already at slot 1, nothing to do
  if (s_num_favorites > 1 && strcmp(s_favorite_files[1], filename) == 0) {
    update_shapes_favorite_buttons();
    return;
  }
  // If at slot 0
  if (s_num_favorites > 0 && strcmp(s_favorite_files[0], filename) == 0) {
    if (s_num_favorites == 2) {
      char tmp[64];
      strncpy(tmp, s_favorite_files[0], sizeof(tmp) - 1);
      strncpy(s_favorite_files[0], s_favorite_files[1], sizeof(s_favorite_files[0]) - 1);
      strncpy(s_favorite_files[1], tmp, sizeof(s_favorite_files[1]) - 1);
    }
    update_shapes_favorite_buttons();
    return;
  }

  // FIFO Queue: evict slot 0, move slot 1 -> slot 0, put new in slot 1
  if (s_num_favorites == 0) {
    strncpy(s_favorite_files[0], filename, sizeof(s_favorite_files[0]) - 1);
    s_favorite_files[0][sizeof(s_favorite_files[0]) - 1] = '\0';
    s_num_favorites = 1;
  } else if (s_num_favorites == 1) {
    strncpy(s_favorite_files[1], filename, sizeof(s_favorite_files[1]) - 1);
    s_favorite_files[1][sizeof(s_favorite_files[1]) - 1] = '\0';
    s_num_favorites = 2;
  } else {
    strncpy(s_favorite_files[0], s_favorite_files[1], sizeof(s_favorite_files[0]) - 1);
    s_favorite_files[0][sizeof(s_favorite_files[0]) - 1] = '\0';
    strncpy(s_favorite_files[1], filename, sizeof(s_favorite_files[1]) - 1);
    s_favorite_files[1][sizeof(s_favorite_files[1]) - 1] = '\0';
    s_num_favorites = 2;
  }
  update_shapes_favorite_buttons();

  machine_settings_t cfg;
  settings_manager_get(&cfg);
  strncpy(cfg.favorite_files[0], s_favorite_files[0], sizeof(cfg.favorite_files[0]) - 1);
  strncpy(cfg.favorite_files[1], s_favorite_files[1], sizeof(cfg.favorite_files[1]) - 1);
  cfg.num_favorites = s_num_favorites;
  settings_manager_save(&cfg);
}

static void on_sd_options_fav_cb(lv_event_t *e) {
  ui_add_favorite_file(s_sd_modal_target_file);
  ui_hide_sd_file_options_modal();
  ui_update_status("FAVORITO GUARDADO", FOCAL_HEX_PASTEL_AMBER);
}

static void on_sd_options_del_cb(lv_event_t *e) {
  ui_hide_sd_file_options_modal();
  if (s_sd_del_modal) {
    if (s_sd_del_msg_lbl) {
      char msg[128];
      snprintf(msg, sizeof(msg), "Deseas eliminar permanentemente\n'%s' de la SD?", s_sd_modal_target_file);
      lv_label_set_text(s_sd_del_msg_lbl, msg);
    }
    lv_obj_clear_flag(s_sd_del_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

static void on_sd_options_cancel_cb(lv_event_t *e) {
  ui_hide_sd_file_options_modal();
}

static void on_sd_del_confirm_cb(lv_event_t *e) {
  if (s_sd_modal_target_idx >= 0 && s_sd_modal_target_idx < NUM_SD_SAMPLE_FILES) {
    s_sd_file_deleted[s_sd_modal_target_idx] = true;
    if (s_sd_file_items[s_sd_modal_target_idx]) {
      lv_obj_t *parent_list = lv_obj_get_parent(s_sd_file_items[s_sd_modal_target_idx]);
      lv_obj_del(s_sd_file_items[s_sd_modal_target_idx]);
      s_sd_file_items[s_sd_modal_target_idx] = NULL;
      if (parent_list) {
        lv_obj_update_layout(parent_list);
      }
    }
    // If the deleted file was the one currently previewed, select the next non-deleted file
    if (strcmp(s_selected_file, s_sd_modal_target_file) == 0) {
      for (int i = 0; i < NUM_SD_SAMPLE_FILES; i++) {
        if (!s_sd_file_deleted[i]) {
          strncpy(s_selected_file, s_sd_files[i], sizeof(s_selected_file) - 1);
          if (s_lbl_file_name) lv_label_set_text(s_lbl_file_name, s_selected_file);
          if (s_sd_preview) {
            gcode_preview_render_sync(s_sd_preview, s_selected_file, on_preview_ready_cb, NULL);
          }
          break;
        }
      }
    }
    // If it was favorite, remove it and leave space empty!
    bool fav_changed = false;
    if (s_num_favorites > 0 && strcmp(s_favorite_files[0], s_sd_modal_target_file) == 0) {
      if (s_num_favorites == 2) {
        strncpy(s_favorite_files[0], s_favorite_files[1], sizeof(s_favorite_files[0]) - 1);
        s_favorite_files[0][sizeof(s_favorite_files[0]) - 1] = '\0';
        s_favorite_files[1][0] = '\0';
        s_num_favorites = 1;
      } else {
        s_favorite_files[0][0] = '\0';
        s_num_favorites = 0;
      }
      fav_changed = true;
    } else if (s_num_favorites > 1 && strcmp(s_favorite_files[1], s_sd_modal_target_file) == 0) {
      s_favorite_files[1][0] = '\0';
      s_num_favorites = 1;
      fav_changed = true;
    }

    if (fav_changed) {
      if (s_active_favorite_idx >= s_num_favorites) {
        ui_shapes_select_shape(0);
      }
      update_shapes_favorite_buttons();
      machine_settings_t cfg;
      settings_manager_get(&cfg);
      strncpy(cfg.favorite_files[0], s_favorite_files[0], sizeof(cfg.favorite_files[0]) - 1);
      strncpy(cfg.favorite_files[1], s_favorite_files[1], sizeof(cfg.favorite_files[1]) - 1);
      cfg.num_favorites = s_num_favorites;
      settings_manager_save(&cfg);
    }
    ui_update_status("ARCHIVO ELIMINADO", FOCAL_HEX_PASTEL_RED);
  }
  if (s_sd_del_modal) {
    lv_obj_add_flag(s_sd_del_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

static void on_sd_del_cancel_cb(lv_event_t *e) {
  if (s_sd_del_modal) {
    lv_obj_add_flag(s_sd_del_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_show_sd_file_options_modal(const char *filename, int file_idx) {
  if (!filename) return;
  strncpy(s_sd_modal_target_file, filename, sizeof(s_sd_modal_target_file) - 1);
  s_sd_modal_target_idx = file_idx;

  if (s_sd_options_modal) {
    if (s_sd_options_file_lbl) {
      lv_label_set_text(s_sd_options_file_lbl, filename);
    }
    lv_obj_clear_flag(s_sd_options_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_hide_sd_file_options_modal(void) {
  if (s_sd_options_modal) {
    lv_obj_add_flag(s_sd_options_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

static void on_sd_lp_timer_cb(lv_timer_t *t) {
  int idx = (int)(uintptr_t)t->user_data;
  s_sd_lp_timer = NULL;
  s_sd_lp_fired = true;
  if (idx >= 0 && idx < NUM_SD_SAMPLE_FILES && !s_sd_file_deleted[idx]) {
    ui_show_sd_file_options_modal(s_sd_files[idx], idx);
  }
}

// Floating Safety Confirmation Modal for Cutting
static lv_obj_t *s_cut_modal = NULL;
static lv_obj_t *s_cut_modal_file_lbl = NULL;
static lv_obj_t *s_cut_modal_info_lbl = NULL;
static char s_pending_cut_file[64] = "prueba_mario.gcode";
static uint32_t s_pending_lines = 9057;
static uint32_t s_pending_est_sec = 210;

static void on_cut_modal_cancel_cb(lv_event_t *e) { ui_close_cut_modal(); }

static void on_cut_modal_confirm_cb(lv_event_t *e) {
  ui_close_cut_modal();
  ui_switch_screen(UI_SCREEN_CUT_MONITOR);
  ui_set_cutting_state(true);
  ui_update_status("CORTANDO", FOCAL_HEX_PASTEL_MINT);
  if (s_cut_liquid_wave) {
    liquid_wave_progress_load_file(s_cut_liquid_wave, s_pending_cut_file);
    liquid_wave_progress_set_progress(s_cut_liquid_wave, 0.0f);
  }
  if (s_mon_lbl_filename) {
    lv_label_set_text(s_mon_lbl_filename, s_pending_cut_file);
  }
}

static void on_preview_ready_cb(gcode_preview_view_t *view,
                                const gcode_preview_meta_t *meta,
                                void *user_data) {
  (void)view;
  (void)user_data;
  if (meta) {
    s_pending_lines = meta->total_lines;
    s_pending_est_sec = meta->estimated_sec;
    if (s_lbl_file_info) {
      char tbuf[64];
      snprintf(tbuf, sizeof(tbuf), "Duracion: %s", meta->formatted_time);
      lv_label_set_text(s_lbl_file_info, tbuf);
    }
  }
}

static void on_file_select_cb(lv_event_t *e) {
  const char *name = (const char *)lv_event_get_user_data(e);
  if (!name) return;
  strncpy(s_selected_file, name, sizeof(s_selected_file) - 1);
  if (s_lbl_file_name)
    lv_label_set_text(s_lbl_file_name, s_selected_file);
  if (s_lbl_file_info)
    lv_label_set_text(s_lbl_file_info, "Cargando silueta...");

  if (s_sd_preview) {
    gcode_preview_load_file_async(s_sd_preview, s_selected_file,
                                  on_preview_ready_cb, NULL);
  }
}

static void on_file_start_cut_cb(lv_event_t *e) {
  ui_show_cut_modal(s_selected_file, s_pending_lines, s_pending_est_sec);
}

static void on_sd_file_touch_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  int idx = (int)(uintptr_t)lv_event_get_user_data(e);

  if (code == LV_EVENT_PRESSED) {
    s_sd_lp_fired = false;
    s_sd_lp_file_idx = idx;
    if (s_sd_lp_timer) {
      lv_timer_del(s_sd_lp_timer);
      s_sd_lp_timer = NULL;
    }
    // Strict 700 ms sustained press timer
    s_sd_lp_timer = lv_timer_create(on_sd_lp_timer_cb, 700, (void *)(uintptr_t)idx);
    lv_timer_set_repeat_count(s_sd_lp_timer, 1);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (s_sd_lp_timer) {
      lv_timer_del(s_sd_lp_timer);
      s_sd_lp_timer = NULL;
    }
  } else if (code == LV_EVENT_CLICKED) {
    if (s_sd_lp_timer) {
      lv_timer_del(s_sd_lp_timer);
      s_sd_lp_timer = NULL;
    }
    if (s_sd_lp_fired) {
      // Sustained press already opened file options dialog; suppress normal click
      return;
    }
    if (idx >= 0 && idx < NUM_SD_SAMPLE_FILES && !s_sd_file_deleted[idx]) {
      lv_event_t ev = *e;
      ev.user_data = (void *)s_sd_files[idx];
      on_file_select_cb(&ev);
    }
  }
}

static void create_sd_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  s_screens[UI_SCREEN_SD_FILES] = scr;
  lv_obj_add_style(scr, &focal_style_screen, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_screen_header(UI_SCREEN_SD_FILES, "Archivos SD", true);

  // Left Card: Scrollable File List with dynamic vertical flex flow
  lv_obj_t *list_card = lv_obj_create(scr);
  lv_obj_set_size(list_card, 240, 260);
  lv_obj_set_pos(list_card, 10, 48);
  lv_obj_add_style(list_card, &focal_style_card, 0);
  lv_obj_set_style_pad_all(list_card, 6, 0);
  lv_obj_set_style_pad_row(list_card, 6, 0);
  lv_obj_set_flex_flow(list_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list_card, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list_card, LV_SCROLLBAR_MODE_AUTO);

  for (int i = 0; i < NUM_SD_SAMPLE_FILES; i++) {
    lv_obj_t *item =
        focal_create_btn(list_card, 216, 48, &focal_icon_files, s_sd_files[i],
                          &focal_style_btn_secondary, FOCAL_HEX_PASTEL_MINT);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(item, 8, 0);
    lv_obj_set_ext_click_area(item, 4);

    lv_obj_t *lbl = lv_obj_get_child(item, 1);
    if (lbl && lv_obj_check_type(lbl, &lv_label_class)) {
      lv_obj_set_size(lbl, 168, 18);
      lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }

    lv_obj_add_event_cb(item, on_sd_file_touch_cb, LV_EVENT_ALL,
                        (void *)(uintptr_t)i);
    s_sd_file_items[i] = item;
  }

  // Right Card: File Details & 2D G-Code Silhouette Preview
  lv_obj_t *det_card = lv_obj_create(scr);
  lv_obj_set_size(det_card, 210, 260);
  lv_obj_set_pos(det_card, 260, 48);
  lv_obj_add_style(det_card, &focal_style_card, 0);
  lv_obj_set_style_pad_all(det_card, 0, 0);
  lv_obj_clear_flag(det_card, LV_OBJ_FLAG_SCROLLABLE);

  // File Name with Ellipsis (single line, height 18px, centered horizontally)
  s_lbl_file_name = lv_label_create(det_card);
  lv_label_set_text(s_lbl_file_name, s_selected_file);
  lv_obj_set_style_text_color(s_lbl_file_name, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_style_text_font(s_lbl_file_name, FOCAL_FONT, 0);
  lv_obj_set_size(s_lbl_file_name, 194, 18);
  lv_label_set_long_mode(s_lbl_file_name, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(s_lbl_file_name, 8, 10);

  // Duration label (centered horizontally)
  s_lbl_file_info = lv_label_create(det_card);
  lv_label_set_text(s_lbl_file_info, "Duracion: ~03:30 min");
  lv_obj_set_style_text_color(
      s_lbl_file_info,
      s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x4B5563), 0);
  lv_obj_set_style_text_font(s_lbl_file_info, FOCAL_FONT, 0);
  lv_obj_set_size(s_lbl_file_info, 194, 16);
  lv_obj_set_pos(s_lbl_file_info, 8, 30);

  // 2D G-Code Silhouette Canvas Preview (180x154 px, centered horizontally at 15)
  s_sd_preview = gcode_preview_create(det_card, 180, 154);
  lv_obj_t *prev_obj = gcode_preview_get_obj(s_sd_preview);
  if (prev_obj) {
    lv_obj_set_pos(prev_obj, 15, 50);
  }

  // Button: Iniciar Corte (height 36px, radius FOCAL_RADIUS_S, centered horizontally at 8)
  lv_obj_t *btn_run =
      focal_create_btn(det_card, 194, 36, &focal_icon_play, "Iniciar Corte",
                       &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_set_pos(btn_run, 8, 214);
  lv_obj_set_style_radius(btn_run, FOCAL_RADIUS_S, 0);
  lv_obj_set_ext_click_area(btn_run, 6);
  lv_obj_add_event_cb(btn_run, on_file_start_cut_cb, LV_EVENT_CLICKED, NULL);
  s_btn_run_cut_lbl = lv_obj_get_child(btn_run, 1);

  // Initial render of default file
  gcode_preview_set_theme(s_sd_preview, s_is_dark);
  gcode_preview_render_sync(s_sd_preview, s_selected_file, on_preview_ready_cb,
                            NULL);

  // ========================================================
  // FLOATING CUT CONFIRMATION & SAFETY MODAL DIALOG OVERLAY
  // ========================================================
  s_cut_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_cut_modal, 480, 320);
  lv_obj_set_pos(s_cut_modal, 0, 0);
  lv_obj_set_style_bg_color(s_cut_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_cut_modal, LV_OPA_70, 0);
  lv_obj_clear_flag(s_cut_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_cut_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *diag = lv_obj_create(s_cut_modal);
  lv_obj_set_size(diag, 380, 240);
  lv_obj_center(diag);
  lv_obj_add_style(diag, &focal_style_card, 0);
  lv_obj_clear_flag(diag, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tbox = lv_obj_create(diag);
  lv_obj_set_size(tbox, 350, 34);
  lv_obj_align(tbox, LV_ALIGN_TOP_MID, 0, -4);
  lv_obj_set_style_bg_opa(tbox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tbox, 0, 0);
  lv_obj_set_style_pad_all(tbox, 0, 0);
  lv_obj_set_flex_flow(tbox, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tbox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(tbox, 6, 0);

  focal_create_icon(tbox, &focal_icon_laser, FOCAL_HEX_PASTEL_AMBER);
  lv_obj_t *lbl_mtitle = lv_label_create(tbox);
  lv_label_set_text(lbl_mtitle, "Confirmar Inicio de Corte");
  lv_obj_set_style_text_color(lbl_mtitle, FOCAL_COLOR_PASTEL_BLUE, 0);
  s_cut_modal_title_lbl = lbl_mtitle;

  s_cut_modal_file_lbl = lv_label_create(diag);
  lv_label_set_text(s_cut_modal_file_lbl, "Archivo: corte_circulo_50mm.gcode");
  lv_obj_set_style_text_color(s_cut_modal_file_lbl, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_align(s_cut_modal_file_lbl, LV_ALIGN_TOP_LEFT, 6, 36);

  s_cut_modal_info_lbl = lv_label_create(diag);
  lv_label_set_text(s_cut_modal_info_lbl,
                    "Lineas: 1420  |  Duracion est: 03:45 min");
  lv_obj_align(s_cut_modal_info_lbl, LV_ALIGN_TOP_LEFT, 6, 60);

  // Safety Alert Box with Yellow Warning Border
  lv_obj_t *warn_box = lv_obj_create(diag);
  lv_obj_set_size(warn_box, 350, 56);
  lv_obj_align(warn_box, LV_ALIGN_TOP_MID, 0, 88);
  lv_obj_set_style_bg_color(
      warn_box, s_is_dark ? lv_color_hex(0x451A03) : lv_color_hex(0xFEF3C7), 0);
  lv_obj_set_style_border_color(warn_box, lv_color_hex(0xF59E0B), 0);
  lv_obj_set_style_border_width(warn_box, 1, 0);
  lv_obj_set_style_radius(warn_box, 6, 0);
  lv_obj_clear_flag(warn_box, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_warn = lv_label_create(warn_box);
  lv_label_set_text(lbl_warn, "ATENCION: Use gafas de proteccion laser.\nEl "
                              "cabezal se movera y el haz se activara.");
  lv_obj_set_style_text_color(
      lbl_warn, s_is_dark ? lv_color_hex(0xFDE68A) : lv_color_hex(0x92400E), 0);
  lv_obj_center(lbl_warn);

  // Bottom Action Buttons: Cancelar vs Iniciar Corte Ahora
  lv_obj_t *btn_mcanc = focal_create_btn(diag, 120, 42, NULL, "Cancelar",
                                         &focal_style_btn_secondary, 0);
  lv_obj_align(btn_mcanc, LV_ALIGN_BOTTOM_LEFT, 6, -4);
  lv_obj_add_event_cb(btn_mcanc, on_cut_modal_cancel_cb, LV_EVENT_CLICKED,
                      NULL);
  s_cut_modal_cancel_lbl = lv_obj_get_child(btn_mcanc, 0);
  if (s_cut_modal_cancel_lbl) {
    lv_obj_set_flex_grow(s_cut_modal_cancel_lbl, 0);
    lv_obj_set_style_text_align(s_cut_modal_cancel_lbl, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_set_flex_align(btn_mcanc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *btn_mstart =
      focal_create_btn(diag, 206, 42, &focal_icon_play, "Iniciar Corte Ahora",
                       &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_align(btn_mstart, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
  lv_obj_add_event_cb(btn_mstart, on_cut_modal_confirm_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_set_flex_flow(btn_mstart, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_mstart, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(btn_mstart, 8, 0);
  lv_obj_set_style_pad_left(btn_mstart, 0, 0);
  lv_obj_set_style_pad_right(btn_mstart, 0, 0);
  s_cut_modal_start_lbl = lv_obj_get_child(btn_mstart, 1);
  if (s_cut_modal_start_lbl) {
    lv_obj_set_flex_grow(s_cut_modal_start_lbl, 0);
    lv_obj_set_style_text_align(s_cut_modal_start_lbl, LV_TEXT_ALIGN_CENTER, 0);
  }

  // ========================================================
  // MODAL 1: FILE ACTIONS (FAVORITO / ELIMINAR / CANCELAR)
  // ========================================================
  s_sd_options_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_sd_options_modal, 480, 320);
  lv_obj_set_pos(s_sd_options_modal, 0, 0);
  lv_obj_set_style_bg_color(s_sd_options_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_sd_options_modal, LV_OPA_70, 0);
  lv_obj_clear_flag(s_sd_options_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_sd_options_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *opt_diag = lv_obj_create(s_sd_options_modal);
  lv_obj_set_size(opt_diag, 350, 220);
  lv_obj_center(opt_diag);
  lv_obj_add_style(opt_diag, &focal_style_card, 0);
  lv_obj_clear_flag(opt_diag, LV_OBJ_FLAG_SCROLLABLE);

  // File Name Header
  s_sd_options_file_lbl = lv_label_create(opt_diag);
  lv_label_set_text(s_sd_options_file_lbl, "archivo.gcode");
  lv_obj_set_style_text_color(s_sd_options_file_lbl, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_style_text_font(s_sd_options_file_lbl, FOCAL_FONT, 0);
  lv_obj_set_size(s_sd_options_file_lbl, 310, 20);
  lv_label_set_long_mode(s_sd_options_file_lbl, LV_LABEL_LONG_DOT);
  lv_obj_align(s_sd_options_file_lbl, LV_ALIGN_TOP_MID, 0, 2);

  lv_obj_t *lbl_opt_sub = lv_label_create(opt_diag);
  lv_label_set_text(lbl_opt_sub, "Opciones de Archivo MicroSD");
  lv_obj_set_style_text_color(
      lbl_opt_sub,
      s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x4B5563), 0);
  lv_obj_align(lbl_opt_sub, LV_ALIGN_TOP_MID, 0, 24);
  s_sd_opt_title_lbl = lbl_opt_sub;

  // Action 1: Marcar como Favorito
  lv_obj_t *btn_opt_fav = focal_create_btn(
      opt_diag, 310, 38, &focal_icon_star, "Marcar como Favorito",
      &focal_style_btn_secondary, FOCAL_HEX_PASTEL_AMBER);
  lv_obj_align(btn_opt_fav, LV_ALIGN_TOP_MID, 0, 52);
  lv_obj_add_event_cb(btn_opt_fav, on_sd_options_fav_cb, LV_EVENT_CLICKED, NULL);
  s_sd_opt_fav_lbl = lv_obj_get_child(btn_opt_fav, 1);

  // Action 2: Eliminar Archivo
  lv_obj_t *btn_opt_del = focal_create_btn(
      opt_diag, 310, 38, &focal_icon_trash, "Eliminar Archivo",
      &focal_style_btn_secondary, FOCAL_HEX_PASTEL_RED);
  lv_obj_align(btn_opt_del, LV_ALIGN_TOP_MID, 0, 98);
  lv_obj_add_event_cb(btn_opt_del, on_sd_options_del_cb, LV_EVENT_CLICKED, NULL);
  s_sd_opt_del_lbl = lv_obj_get_child(btn_opt_del, 1);

  // Action 3: Cancelar
  lv_obj_t *btn_opt_canc = focal_create_btn(
      opt_diag, 310, 34, NULL, "Cancelar",
      &focal_style_btn_secondary, 0);
  lv_obj_align(btn_opt_canc, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_add_event_cb(btn_opt_canc, on_sd_options_cancel_cb, LV_EVENT_CLICKED, NULL);
  s_sd_opt_canc_lbl = lv_obj_get_child(btn_opt_canc, 0);

  // ========================================================
  // MODAL 2: DELETE CONFIRMATION SAFETY MODAL
  // ========================================================
  s_sd_del_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_sd_del_modal, 480, 320);
  lv_obj_set_pos(s_sd_del_modal, 0, 0);
  lv_obj_set_style_bg_color(s_sd_del_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_sd_del_modal, LV_OPA_70, 0);
  lv_obj_clear_flag(s_sd_del_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_sd_del_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *del_diag = lv_obj_create(s_sd_del_modal);
  lv_obj_set_size(del_diag, 340, 190);
  lv_obj_center(del_diag);
  lv_obj_add_style(del_diag, &focal_style_card, 0);
  lv_obj_clear_flag(del_diag, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tbox_del = lv_obj_create(del_diag);
  lv_obj_set_size(tbox_del, 300, 30);
  lv_obj_align(tbox_del, LV_ALIGN_TOP_MID, 0, -4);
  lv_obj_set_style_bg_opa(tbox_del, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tbox_del, 0, 0);
  lv_obj_set_style_pad_all(tbox_del, 0, 0);
  lv_obj_set_flex_flow(tbox_del, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tbox_del, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(tbox_del, 6, 0);

  focal_create_icon(tbox_del, &focal_icon_trash, FOCAL_HEX_PASTEL_RED);
  lv_obj_t *lbl_del_t = lv_label_create(tbox_del);
  lv_label_set_text(lbl_del_t, "Confirmar Eliminacion");
  lv_obj_set_style_text_color(lbl_del_t, FOCAL_COLOR_PASTEL_RED, 0);
  s_sd_del_title_lbl = lbl_del_t;

  s_sd_del_msg_lbl = lv_label_create(del_diag);
  lv_label_set_text(s_sd_del_msg_lbl, "Deseas eliminar permanentemente\neste archivo de la tarjeta SD?");
  lv_obj_set_style_text_align(s_sd_del_msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_sd_del_msg_lbl, LV_ALIGN_CENTER, 0, -8);

  lv_obj_t *btn_del_cancel = focal_create_btn(
      del_diag, 140, 38, NULL, "Cancelar",
      &focal_style_btn_secondary, 0);
  lv_obj_align(btn_del_cancel, LV_ALIGN_BOTTOM_LEFT, 6, -4);
  lv_obj_add_event_cb(btn_del_cancel, on_sd_del_cancel_cb, LV_EVENT_CLICKED, NULL);
  s_sd_del_canc_lbl = lv_obj_get_child(btn_del_cancel, 0);

  lv_obj_t *btn_del_ok = focal_create_btn(
      del_diag, 140, 38, &focal_icon_trash, "Eliminar",
      &focal_style_btn_secondary, FOCAL_HEX_PASTEL_RED);
  lv_obj_align(btn_del_ok, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
  lv_obj_add_event_cb(btn_del_ok, on_sd_del_confirm_cb, LV_EVENT_CLICKED, NULL);
  s_sd_del_conf_lbl = lv_obj_get_child(btn_del_ok, 1);
}

// =============================================================================
// SCREEN 3: MONITOR DE CORTE Y TELEMETRIA EN VIVO (SPLIT LAYOUT)
// =============================================================================
static void update_mon_pause_btn(bool is_cutting) {
  if (!s_mon_btn_pause_resume) return;
  lv_obj_remove_style(s_mon_btn_pause_resume, &focal_style_btn_amber, 0);
  lv_obj_remove_style(s_mon_btn_pause_resume, &focal_style_btn_mint, 0);

  lv_obj_t *img = lv_obj_get_child(s_mon_btn_pause_resume, 0);
  lv_obj_t *lbl = lv_obj_get_child(s_mon_btn_pause_resume, 1);

  if (is_cutting) {
    lv_obj_add_style(s_mon_btn_pause_resume, &focal_style_btn_amber, 0);
    if (img) {
      lv_img_set_src(img, &focal_icon_pause);
      lv_obj_set_style_img_recolor(img, lv_color_hex(0xFFFFFF), 0);
    }
    if (lbl) {
      lv_label_set_text(lbl, "Pausar");
      lv_obj_set_flex_grow(lbl, 0);
    }
  } else {
    lv_obj_add_style(s_mon_btn_pause_resume, &focal_style_btn_mint, 0);
    if (img) {
      lv_img_set_src(img, &focal_icon_play);
      lv_obj_set_style_img_recolor(img, lv_color_hex(0xFFFFFF), 0);
    }
    if (lbl) {
      lv_label_set_text(lbl, "Reanudar");
      lv_obj_set_flex_grow(lbl, 0);
    }
  }
}

static void on_cut_light_toggle_cb(lv_event_t *e) {
  (void)e;
  s_machine_light_on = !s_machine_light_on;
  ESP_LOGI(TAG, "Luz de maquina: %s", s_machine_light_on ? "ON" : "OFF");

  if (!s_mon_btn_led) return;
  lv_obj_t *img = lv_obj_get_child(s_mon_btn_led, 0);
  lv_obj_t *lbl = lv_obj_get_child(s_mon_btn_led, 1);

  lv_obj_remove_style(s_mon_btn_led, &focal_style_btn_secondary, 0);
  lv_obj_remove_style(s_mon_btn_led, &focal_style_btn_amber, 0);

  if (s_machine_light_on) {
    lv_obj_add_style(s_mon_btn_led, &focal_style_btn_amber, 0);
    if (img) lv_obj_set_style_img_recolor(img, lv_color_hex(0xFFFFFF), 0);
    if (lbl) {
      lv_label_set_text(lbl, "ON");
      lv_obj_set_flex_grow(lbl, 0);
    }
  } else {
    lv_obj_add_style(s_mon_btn_led, &focal_style_btn_secondary, 0);
    if (img) lv_obj_set_style_img_recolor(img, lv_color_hex(FOCAL_HEX_PASTEL_AMBER), 0);
    if (lbl) {
      lv_label_set_text(lbl, "OFF");
      lv_obj_set_flex_grow(lbl, 0);
    }
  }
}

static void on_cut_pause_resume_cb(lv_event_t *e) {
  (void)e;
  char resp[32];
  if (s_is_cutting) {
    gcode_parser_execute_line("!", resp, sizeof(resp));
    ui_set_cutting_state(false);
    ui_update_status("EN PAUSA", FOCAL_HEX_PASTEL_AMBER);
  } else {
    gcode_parser_execute_line("~", resp, sizeof(resp));
    ui_set_cutting_state(true);
    ui_update_status("CORTANDO", FOCAL_HEX_PASTEL_MINT);
  }
}

static void on_cut_stop_requested_cb(lv_event_t *e) {
  (void)e;
  ui_show_stop_modal(true);
}

static void on_stop_modal_cancel_cb(lv_event_t *e) {
  (void)e;
  ui_show_stop_modal(false);
}

static void on_stop_modal_confirm_cb(lv_event_t *e) {
  (void)e;
  ui_show_stop_modal(false);
  laser_set_power(0);
  laser_set_enable(false);
  char resp[32];
  gcode_parser_execute_line("!", resp, sizeof(resp));
  ui_set_cutting_state(false);
  ui_update_status("DETENIDO", FOCAL_HEX_PASTEL_RED);
  ui_switch_screen(UI_SCREEN_LAUNCHER);
}

static void create_cut_monitor_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  s_screens[UI_SCREEN_CUT_MONITOR] = scr;
  lv_obj_add_style(scr, &focal_style_screen, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_screen_header(UI_SCREEN_CUT_MONITOR, "Monitor en Vivo", true);

  // -------------------------------------------------------------------------
  // LEFT CARD: LIQUID WAVE PROGRESS (210 x 264 px at x=10, y=46)
  // -------------------------------------------------------------------------
  lv_obj_t *card_left = lv_obj_create(scr);
  lv_obj_set_size(card_left, 210, 264);
  lv_obj_set_pos(card_left, 10, 46);
  lv_obj_add_style(card_left, &focal_style_card, 0);
  lv_obj_clear_flag(card_left, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(card_left, 4, 0);

  s_mon_lbl_pct = lv_label_create(card_left);
  lv_label_set_text(s_mon_lbl_pct, "45.0%");
  lv_obj_set_style_text_color(s_mon_lbl_pct, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_set_style_text_font(s_mon_lbl_pct, FOCAL_FONT, 0);
  lv_obj_align(s_mon_lbl_pct, LV_ALIGN_TOP_MID, 0, 4);

  // Instantiate 3-layer Liquid Wave Progress Component (184 x 188 px)
  s_cut_liquid_wave = liquid_wave_progress_create(card_left, 184, 188);
  if (s_cut_liquid_wave) {
    lv_obj_t *wave_obj = liquid_wave_progress_get_obj(s_cut_liquid_wave);
    lv_obj_align(wave_obj, LV_ALIGN_CENTER, 0, 10);
  }

  // -------------------------------------------------------------------------
  // RIGHT CARD: SYMMETRICAL CONTROL STACK (240 x 264 px at x=230, y=46)
  // -------------------------------------------------------------------------
  lv_obj_t *card_right = lv_obj_create(scr);
  lv_obj_set_size(card_right, 240, 264);
  lv_obj_set_pos(card_right, 230, 46);
  lv_obj_add_style(card_right, &focal_style_card, 0);
  lv_obj_set_style_pad_all(card_right, 0, 0);
  lv_obj_clear_flag(card_right, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Clean Information Typography (No heavy pill wrappers)
  s_mon_lbl_filename = lv_label_create(card_right);
  lv_label_set_text(s_mon_lbl_filename, s_selected_file);
  lv_label_set_long_mode(s_mon_lbl_filename, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(s_mon_lbl_filename, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_style_text_font(s_mon_lbl_filename, FOCAL_FONT, 0);
  lv_obj_set_size(s_mon_lbl_filename, 216, 20);
  lv_obj_set_pos(s_mon_lbl_filename, 12, 14);

  s_mon_lbl_elapsed = lv_label_create(card_right);
  lv_label_set_text(s_mon_lbl_elapsed, "Transcurrido: 02:15 min");
  lv_obj_set_style_text_color(
      s_mon_lbl_elapsed,
      s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
  lv_obj_set_style_text_font(s_mon_lbl_elapsed, FOCAL_FONT, 0);
  lv_obj_set_size(s_mon_lbl_elapsed, 216, 18);
  lv_obj_set_pos(s_mon_lbl_elapsed, 12, 40);

  s_mon_lbl_remaining = lv_label_create(card_right);
  lv_label_set_text(s_mon_lbl_remaining, "Restante: 08:35 min");
  lv_obj_set_style_text_color(s_mon_lbl_remaining, FOCAL_COLOR_PASTEL_AMBER, 0);
  lv_obj_set_style_text_font(s_mon_lbl_remaining, FOCAL_FONT, 0);
  lv_obj_set_size(s_mon_lbl_remaining, 216, 18);
  lv_obj_set_pos(s_mon_lbl_remaining, 12, 62);

  s_mon_lbl_lines = lv_label_create(card_right);
  lv_label_set_text(s_mon_lbl_lines, "Lineas: 4080 / 9057");
  lv_obj_set_style_text_color(
      s_mon_lbl_lines,
      s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x6B7280), 0);
  lv_obj_set_style_text_font(s_mon_lbl_lines, FOCAL_FONT, 0);
  lv_obj_set_size(s_mon_lbl_lines, 216, 18);
  lv_obj_set_pos(s_mon_lbl_lines, 12, 84);

  // 2. Middle Row: LED Toggle (ON/OFF) + Stop Button in the same line
  s_mon_btn_led = focal_create_btn(card_right, 104, 40, &focal_icon_light,
                                   s_machine_light_on ? "ON" : "OFF",
                                   &focal_style_btn_secondary,
                                   FOCAL_HEX_PASTEL_AMBER);
  lv_obj_set_pos(s_mon_btn_led, 12, 130);
  lv_obj_set_ext_click_area(s_mon_btn_led, 6);
  lv_obj_add_event_cb(s_mon_btn_led, on_cut_light_toggle_cb,
                      LV_EVENT_CLICKED, NULL);

  s_mon_btn_stop = focal_create_btn(card_right, 104, 40, &focal_icon_stop,
                                    "Detener", &focal_style_btn_danger,
                                    0xFFFFFF);
  lv_obj_set_pos(s_mon_btn_stop, 124, 130);
  lv_obj_set_ext_click_area(s_mon_btn_stop, 6);
  s_mon_btn_stop_lbl = lv_obj_get_child(s_mon_btn_stop, 1);
  lv_obj_add_event_cb(s_mon_btn_stop, on_cut_stop_requested_cb,
                      LV_EVENT_CLICKED, NULL);

  // 3. Bottom Row: Pause / Resume Button (centered icon and label)
  s_mon_btn_pause_resume = focal_create_btn(card_right, 216, 44,
                                            &focal_icon_pause, "Pausar",
                                            &focal_style_btn_amber, 0xFFFFFF);
  lv_obj_set_pos(s_mon_btn_pause_resume, 12, 188);
  lv_obj_set_ext_click_area(s_mon_btn_pause_resume, 6);
  lv_obj_t *lbl_pr = lv_obj_get_child(s_mon_btn_pause_resume, 1);
  if (lbl_pr) {
    lv_obj_set_flex_grow(lbl_pr, 0);
  }
  s_mon_btn_pause_lbl = lbl_pr;
  lv_obj_add_event_cb(s_mon_btn_pause_resume, on_cut_pause_resume_cb,
                      LV_EVENT_CLICKED, NULL);

  // =========================================================================
  // SAFETY CONFIRMATION MODAL FOR STOPPING A CUT
  // =========================================================================
  s_stop_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_stop_modal, 480, 320);
  lv_obj_set_pos(s_stop_modal, 0, 0);
  lv_obj_set_style_bg_color(s_stop_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_stop_modal, LV_OPA_70, 0);
  lv_obj_clear_flag(s_stop_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_stop_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *sdiag = lv_obj_create(s_stop_modal);
  lv_obj_set_size(sdiag, 380, 210);
  lv_obj_center(sdiag);
  lv_obj_add_style(sdiag, &focal_style_card, 0);
  lv_obj_clear_flag(sdiag, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *stbox = lv_obj_create(sdiag);
  lv_obj_set_size(stbox, 350, 32);
  lv_obj_align(stbox, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_flex_flow(stbox, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(stbox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(stbox, 0, 0);
  lv_obj_set_style_bg_opa(stbox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(stbox, 0, 0);

  focal_create_icon(stbox, &focal_icon_stop, FOCAL_HEX_PASTEL_RED);
  lv_obj_t *lbl_stitle = lv_label_create(stbox);
  lv_label_set_text(lbl_stitle, "Detener Proceso de Corte?");
  lv_obj_set_style_text_color(lbl_stitle, FOCAL_COLOR_PASTEL_RED, 0);
  s_stop_modal_title_lbl = lbl_stitle;

  lv_obj_t *lbl_sdesc = lv_label_create(sdiag);
  lv_label_set_text(lbl_sdesc,
                    "El trabajo actual se cancelara de inmediato y el laser "
                    "se apagara por seguridad.");
  lv_obj_set_style_text_color(
      lbl_sdesc, s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x4B5563),
      0);
  lv_label_set_long_mode(lbl_sdesc, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(lbl_sdesc, 340);
  lv_obj_align(lbl_sdesc, LV_ALIGN_TOP_MID, 0, 42);
  s_stop_modal_msg_lbl = lbl_sdesc;

  lv_obj_t *sbtn_canc = focal_create_btn(sdiag, 150, 42, NULL, "Continuar",
                                         &focal_style_btn_secondary, 0);
  lv_obj_align(sbtn_canc, LV_ALIGN_BOTTOM_LEFT, 10, -8);
  lv_obj_add_event_cb(sbtn_canc, on_stop_modal_cancel_cb, LV_EVENT_CLICKED,
                      NULL);
  s_stop_modal_cont_lbl = lv_obj_get_child(sbtn_canc, 0);

  lv_obj_t *sbtn_stop = focal_create_btn(sdiag, 150, 42, &focal_icon_stop,
                                         "Detener", &focal_style_btn_danger,
                                         0xFFFFFF);
  lv_obj_align(sbtn_stop, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
  lv_obj_add_event_cb(sbtn_stop, on_stop_modal_confirm_cb, LV_EVENT_CLICKED,
                      NULL);
  s_stop_modal_stop_lbl = lv_obj_get_child(sbtn_stop, 1);
}

// =============================================================================
// SCREEN 4: ASISTENTE DE NIVELACION MANUAL DE CAMA (160x200 mm - 4 TORNILLOS +
// CENTRO)
// =============================================================================
typedef struct {
  int x;
  int y;
  const char *step_name;
  const char *screw_label;
  const char *instructions;
} calib_step_info_t;

static const calib_step_info_t s_calib_steps[5] = {
    {10, 10, "Paso 1/5: Frontal Izq", "T1 (Frontal-Izq)",
     "Gira la perilla inferior-izquierda hasta que la galga o tarjeta pase "
     "rozando la boquilla."},
    {150, 10, "Paso 2/5: Frontal Der", "T2 (Frontal-Der)",
     "Gira la perilla frontal-derecha hasta sentir la misma friccion suave con "
     "la galga."},
    {150, 190, "Paso 3/5: Trasera Der", "T3 (Trasera-Der)",
     "Ajusta la perilla trasera-derecha igualando la altura con las esquinas "
     "frontales."},
    {10, 190, "Paso 4/5: Trasera Izq", "T4 (Trasera-Izq)",
     "Gira la perilla trasera-izquierda comprobando que la distancia sea "
     "uniforme."},
    {80, 100, "Paso 5/5: Centro Cama", "Centro (Test)",
     "Comprobacion central final. La altura debe ser uniforme en toda la "
     "superficie."}};

static const int s_calib_pt_coords[5][2] = {
    {8, 135},   // T1: Frontal Izq (Y=10)
    {100, 135}, // T2: Frontal Der (Y=10)
    {100, 8},   // T3: Trasera Der (Y=190)
    {8, 8},     // T4: Trasera Izq (Y=190)
    {54, 71}    // Centro (Y=100)
};

static bool s_calib_moving = false;
static uint8_t s_calib_done_mask = 0;
static bool s_calib_pointer_on = false;

static lv_obj_t *s_calib_lbl_step = NULL;
static lv_obj_t *s_calib_lbl_instruction = NULL;
static lv_obj_t *s_calib_btn_next = NULL;
static lv_obj_t *s_calib_btn_prev = NULL;
static lv_obj_t *s_calib_btn_ptr = NULL;
static lv_obj_t *s_calib_bar_travel = NULL;
static lv_timer_t *s_calib_timer = NULL;

static void calib_update_view(void);

static void calib_travel_timer_cb(lv_timer_t *timer) {
  s_calib_moving = false;
  s_calib_timer = NULL;
  calib_update_view();
}

static void calib_dispatch_move(int step_idx) {
  int target_x = 10;
  int target_y = 10;
  switch (step_idx) {
    case 0: target_x = 10; target_y = 10; break;
    case 1: target_x = s_bed_w_mm - 10; target_y = 10; break;
    case 2: target_x = s_bed_w_mm - 10; target_y = s_bed_h_mm - 10; break;
    case 3: target_x = 10; target_y = s_bed_h_mm - 10; break;
    default: target_x = s_bed_w_mm / 2; target_y = s_bed_h_mm / 2; break;
  }
  char cmd[48];
  char resp[32];
  snprintf(cmd, sizeof(cmd), "G90 G0 X%d Y%d F3000", target_x, target_y);
  gcode_parser_execute_line(cmd, resp, sizeof(resp));

  s_calib_moving = true;
  if (s_calib_timer) {
    lv_timer_del(s_calib_timer);
    s_calib_timer = NULL;
  }
  s_calib_timer = lv_timer_create(calib_travel_timer_cb, 1400, NULL);
  if (s_calib_timer)
    lv_timer_set_repeat_count(s_calib_timer, 1);
  calib_update_view();
}

static void on_calib_success_ok_cb(lv_event_t *e) {
  if (s_calib_success_modal) {
    lv_obj_add_flag(s_calib_success_modal, LV_OBJ_FLAG_HIDDEN);
  }
  char resp[32];
  gcode_parser_execute_line("$H", resp, sizeof(resp));
  s_calib_step = 0;
  s_calib_done_mask = 0;
  calib_dispatch_move(0);
  calib_update_view();
}

static void on_calib_next_cb(lv_event_t *e) {
  if (s_calib_moving)
    return;

  if (s_calib_step == 4) {
    s_calib_done_mask = 0x1F;
    if (s_calib_success_modal) {
      lv_obj_clear_flag(s_calib_success_modal, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(s_calib_success_modal);
    } else {
      char resp[32];
      gcode_parser_execute_line("$H", resp, sizeof(resp));
      s_calib_step = 0;
      s_calib_done_mask = 0;
      calib_update_view();
    }
    return;
  }

  s_calib_done_mask |= (1 << s_calib_step);
  s_calib_step++;
  calib_dispatch_move(s_calib_step);
}

static void on_calib_prev_cb(lv_event_t *e) {
  if (s_calib_moving)
    return;
  if (s_calib_step > 0) {
    s_calib_step--;
    calib_dispatch_move(s_calib_step);
  }
}

static void on_calib_point_click_cb(lv_event_t *e) {
  if (s_calib_moving)
    return;
  int idx = (int)(uintptr_t)lv_event_get_user_data(e);
  if (idx >= 0 && idx < 5) {
    s_calib_step = idx;
    calib_dispatch_move(s_calib_step);
  }
}

static void on_calib_pointer_toggle_cb(lv_event_t *e) {
  s_calib_pointer_on = !s_calib_pointer_on;
  if (s_calib_pointer_on) {
    laser_set_enable(true);
    laser_set_power(10); // 1%
    if (s_calib_btn_ptr) {
      lv_obj_set_style_bg_color(s_calib_btn_ptr, FOCAL_COLOR_PASTEL_AMBER, 0);
      lv_obj_t *lbl = lv_obj_get_child(
          s_calib_btn_ptr, lv_obj_get_child_cnt(s_calib_btn_ptr) - 1);
      if (lbl)
        lv_label_set_text(lbl, "Puntero ON");
    }
  } else {
    laser_set_power(0);
    laser_set_enable(false);
    if (s_calib_btn_ptr) {
      lv_obj_set_style_bg_color(
          s_calib_btn_ptr,
          s_is_dark ? lv_color_hex(0x262A35) : lv_color_hex(0xE5E7EB), 0);
      lv_obj_t *lbl = lv_obj_get_child(
          s_calib_btn_ptr, lv_obj_get_child_cnt(s_calib_btn_ptr) - 1);
      if (lbl)
        lv_label_set_text(lbl, "Puntero");
    }
  }
}

static void on_calib_reset_cb(lv_event_t *e) {
  s_calib_step = 0;
  s_calib_done_mask = 0;
  calib_dispatch_move(0);
}

void ui_calibration_set_step(int step_idx) {
  if (step_idx >= 0 && step_idx < 5) {
    s_calib_step = step_idx;
    calib_dispatch_move(s_calib_step);
  }
}

static void calib_update_view(void) {
  for (int i = 0; i < 5; i++) {
    if (!s_calib_pts[i])
      continue;
    if (i == s_calib_step) {
      lv_obj_set_style_bg_color(s_calib_pts[i], focal_theme_get_accent_color(),
                                0);
      lv_obj_set_style_border_color(s_calib_pts[i], lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_border_width(s_calib_pts[i], 2, 0);
    } else if (s_calib_done_mask & (1 << i)) {
      lv_obj_set_style_bg_color(s_calib_pts[i], FOCAL_COLOR_PASTEL_MINT, 0);
      lv_obj_set_style_border_color(s_calib_pts[i], lv_color_hex(0x10B981), 0);
      lv_obj_set_style_border_width(s_calib_pts[i], 1, 0);
    } else {
      lv_obj_set_style_bg_color(
          s_calib_pts[i],
          s_is_dark ? lv_color_hex(0x262A35) : lv_color_hex(0xD1D5DB), 0);
      lv_obj_set_style_border_color(
          s_calib_pts[i],
          s_is_dark ? lv_color_hex(0x3B4252) : lv_color_hex(0x9CA3AF), 0);
      lv_obj_set_style_border_width(s_calib_pts[i], 1, 0);
    }
  }

  if (s_calib_laser_dot && s_calib_step >= 0 && s_calib_step < 5 && s_calib_pts[s_calib_step]) {
    lv_obj_align_to(s_calib_laser_dot, s_calib_pts[s_calib_step], LV_ALIGN_CENTER, 0, 0);
  }

  if (s_calib_lbl_step) {
    const char *step_titles[5] = {
        "Paso 1/5: Frontal Izq", "Paso 2/5: Frontal Der",
        "Paso 3/5: Trasera Der", "Paso 4/5: Trasera Izq",
        "Paso 5/5: Centro"};
    if (s_calib_step >= 0 && s_calib_step < 5) {
      lv_label_set_text(s_calib_lbl_step, step_titles[s_calib_step]);
    }
  }

  if (s_calib_moving) {
    if (s_calib_lbl_instruction) {
      lv_label_set_text(
          s_calib_lbl_instruction,
          "Desplazando cabezal al punto...\nPor favor espere a que termine.");
      lv_obj_set_style_text_color(s_calib_lbl_instruction,
                                  FOCAL_COLOR_PASTEL_AMBER, 0);
    }
    if (s_calib_bar_travel) {
      lv_obj_clear_flag(s_calib_bar_travel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_calib_btn_next) {
      lv_obj_t *lbl = lv_obj_get_child(
          s_calib_btn_next, lv_obj_get_child_cnt(s_calib_btn_next) - 1);
      if (lbl)
        lv_label_set_text(lbl, "Moviendo...");
      lv_obj_add_state(s_calib_btn_next, LV_STATE_DISABLED);
    }
    if (s_calib_btn_prev) {
      lv_obj_add_state(s_calib_btn_prev, LV_STATE_DISABLED);
    }
  } else {
    if (s_calib_lbl_instruction) {
      const char *step_descs[5] = {
          "Ajusta la perilla frontal-izquierda usando una hoja de papel hasta "
          "sentir friccion leve.",
          "Ajusta la perilla frontal-derecha con la misma holgura que la "
          "anterior.",
          "Ajusta la perilla trasera-derecha igualando la altura con las "
          "esquinas frontales.",
          "Ajusta la perilla trasera-izquierda para completar el cuadrante.",
          "Verifica que el centro tenga la misma friccion. Si roza mucho o "
          "queda suelto, reajusta las 4 perillas."};
      if (s_calib_step >= 0 && s_calib_step < 5) {
        lv_label_set_text(s_calib_lbl_instruction, step_descs[s_calib_step]);
      }
      lv_obj_set_style_text_color(
          s_calib_lbl_instruction,
          s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
    }
    if (s_calib_bar_travel) {
      lv_obj_add_flag(s_calib_bar_travel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_calib_btn_next) {
      lv_obj_clear_state(s_calib_btn_next, LV_STATE_DISABLED);
      lv_obj_t *lbl = lv_obj_get_child(
          s_calib_btn_next, lv_obj_get_child_cnt(s_calib_btn_next) - 1);
      if (lbl) {
        if (s_calib_step == 4) {
          lv_label_set_text(lbl, "Finalizar");
        } else {
          lv_label_set_text(lbl, "Siguiente");
        }
      }
    }
    if (s_calib_btn_prev) {
      if (s_calib_step > 0) {
        lv_obj_clear_state(s_calib_btn_prev, LV_STATE_DISABLED);
      } else {
        lv_obj_add_state(s_calib_btn_prev, LV_STATE_DISABLED);
      }
    }
  }
}

static void create_calibration_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  s_screens[UI_SCREEN_CALIBRATION] = scr;
  lv_obj_add_style(scr, &focal_style_screen, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_screen_header(UI_SCREEN_CALIBRATION, "Nivelacion de Cama", true);

  // Left Panel: Diagrama visual de cama (140x175 px proporcional a 160x200 mm)
  lv_obj_t *c1 = lv_obj_create(scr);
  lv_obj_set_size(c1, 190, 262);
  lv_obj_set_pos(c1, 10, 46);
  lv_obj_add_style(c1, &focal_style_card, 0);
  lv_obj_set_style_pad_all(c1, 0, 0);
  lv_obj_clear_flag(c1, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *t1 = lv_label_create(c1);
  s_calib_lbl_bed_dim = t1;
  s_calib_lbl_bed = t1;
  char bbuf_cal[48];
  snprintf(bbuf_cal, sizeof(bbuf_cal), "Cama %dx%d mm", s_bed_w_mm, s_bed_h_mm);
  lv_label_set_text(t1, bbuf_cal);
  lv_obj_set_style_text_color(t1, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_width(t1, 180);
  lv_obj_set_style_text_align(t1, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *bed_box = lv_obj_create(c1);
  s_calib_bed_box = bed_box;
  lv_obj_set_size(bed_box, 140, 175);
  lv_obj_align(bed_box, LV_ALIGN_TOP_MID, 0, 38);
  lv_obj_set_style_bg_color(
      bed_box, s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_border_color(
      bed_box, s_is_dark ? lv_color_hex(0x3B82F6) : lv_color_hex(0x2563EB), 0);
  lv_obj_set_style_border_width(bed_box, 1, 0);
  lv_obj_set_style_radius(bed_box, 6, 0);
  lv_obj_set_style_pad_all(bed_box, 0, 0);
  lv_obj_clear_flag(bed_box, LV_OBJ_FLAG_SCROLLABLE);

  const char *pt_labels[5] = {"T1", "T2", "T3", "T4", "C"};

  for (int i = 0; i < 5; i++) {
    lv_obj_t *b = lv_btn_create(bed_box);
    lv_obj_set_size(b, 32, 32);
    lv_obj_set_pos(b, s_calib_pt_coords[i][0], s_calib_pt_coords[i][1]);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_ext_click_area(b, 8);
    s_calib_pts[i] = b;

    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, pt_labels[i]);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(b, on_calib_point_click_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
  }

  // Laser head marker reticle outer ring (38x38 circle cleanly framing 32x32 active point)
  s_calib_laser_dot = lv_obj_create(bed_box);
  lv_obj_set_size(s_calib_laser_dot, 38, 38);
  lv_obj_set_style_radius(s_calib_laser_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_calib_laser_dot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(s_calib_laser_dot, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_set_style_border_width(s_calib_laser_dot, 2, 0);
  lv_obj_clear_flag(s_calib_laser_dot,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  // Right Panel: Wizard controls & instructions (Width 260 at x=210)
  lv_obj_t *c2 = lv_obj_create(scr);
  lv_obj_set_size(c2, 260, 262);
  lv_obj_set_pos(c2, 210, 46);
  lv_obj_add_style(c2, &focal_style_card, 0);
  lv_obj_set_style_pad_all(c2, 0, 0);
  lv_obj_clear_flag(c2, LV_OBJ_FLAG_SCROLLABLE);

  // Step Title
  s_calib_lbl_step = lv_label_create(c2);
  lv_label_set_text(s_calib_lbl_step, "Paso 1/5: Frontal Izq");
  lv_obj_set_style_text_color(s_calib_lbl_step, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_set_pos(s_calib_lbl_step, 12, 10);

  // Instruction Box: Squircle card perfectly centered (Width 236, Left 12, Right 12)
  lv_obj_t *ins_box = lv_obj_create(c2);
  lv_obj_set_size(ins_box, 236, 78);
  lv_obj_set_pos(ins_box, 12, 34);
  lv_obj_add_style(ins_box, &focal_style_pill, 0);
  lv_obj_set_style_radius(ins_box, FOCAL_RADIUS_S, 0);
  lv_obj_set_style_pad_all(ins_box, 8, 0);
  lv_obj_clear_flag(ins_box, LV_OBJ_FLAG_SCROLLABLE);

  s_calib_lbl_instruction = lv_label_create(ins_box);
  lv_obj_set_width(s_calib_lbl_instruction, 220);
  lv_label_set_long_mode(s_calib_lbl_instruction, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_calib_lbl_instruction, s_calib_steps[0].instructions);
  lv_obj_align(s_calib_lbl_instruction, LV_ALIGN_TOP_LEFT, 0, 0);

  // Travel progress bar
  s_calib_bar_travel = lv_bar_create(c2);
  lv_obj_set_size(s_calib_bar_travel, 236, 4);
  lv_obj_set_pos(s_calib_bar_travel, 12, 118);
  lv_obj_set_style_bg_color(s_calib_bar_travel, FOCAL_COLOR_PASTEL_AMBER,
                            LV_PART_INDICATOR);
  lv_bar_set_value(s_calib_bar_travel, 100, LV_ANIM_OFF);
  lv_obj_add_flag(s_calib_bar_travel, LV_OBJ_FLAG_HIDDEN);

  // Utility Row: Laser pointer toggle + Reset (Width 114 each, 8px gap, x=12..248)
  s_calib_btn_ptr =
      focal_create_btn(c2, 114, 38, &focal_icon_laser, "Puntero",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_AMBER);
  lv_obj_set_pos(s_calib_btn_ptr, 12, 128);
  lv_obj_set_ext_click_area(s_calib_btn_ptr, 6);
  lv_obj_add_event_cb(s_calib_btn_ptr, on_calib_pointer_toggle_cb,
                      LV_EVENT_CLICKED, NULL);
  s_calib_btn_pointer_lbl = lv_obj_get_child(s_calib_btn_ptr, 1);

  lv_obj_t *btn_rst =
      focal_create_btn(c2, 114, 38, &focal_icon_refresh, "Reiniciar",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_set_pos(btn_rst, 134, 128);
  lv_obj_set_ext_click_area(btn_rst, 6);
  lv_obj_add_event_cb(btn_rst, on_calib_reset_cb, LV_EVENT_CLICKED, NULL);
  s_calib_btn_reset_lbl = lv_obj_get_child(btn_rst, 1);

  // Bottom Navigation Buttons: Anterior + Siguiente (Width 114 each, 8px gap, x=12..248)
  s_calib_btn_prev = focal_create_btn(c2, 114, 44, NULL, "Anterior",
                                      &focal_style_btn_secondary, 0);
  lv_obj_set_pos(s_calib_btn_prev, 12, 174);
  lv_obj_set_ext_click_area(s_calib_btn_prev, 6);
  lv_obj_add_event_cb(s_calib_btn_prev, on_calib_prev_cb, LV_EVENT_CLICKED,
                      NULL);
  s_calib_btn_prev_lbl = lv_obj_get_child(s_calib_btn_prev, 0);

  s_calib_btn_next =
      focal_create_btn(c2, 114, 44, &focal_icon_check, "Siguiente",
                       &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_set_pos(s_calib_btn_next, 134, 174);
  lv_obj_set_ext_click_area(s_calib_btn_next, 6);
  lv_obj_add_event_cb(s_calib_btn_next, on_calib_next_cb, LV_EVENT_CLICKED,
                      NULL);
  s_calib_btn_next_lbl = lv_obj_get_child(s_calib_btn_next, 1);

  // ========================================================
  // CALIBRATION SUCCESS MODAL OVERLAY
  // ========================================================
  s_calib_success_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_calib_success_modal, 480, 320);
  lv_obj_set_pos(s_calib_success_modal, 0, 0);
  lv_obj_set_style_bg_color(s_calib_success_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_calib_success_modal, LV_OPA_70, 0);
  lv_obj_clear_flag(s_calib_success_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_calib_success_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *suc_diag = lv_obj_create(s_calib_success_modal);
  lv_obj_set_size(suc_diag, 340, 180);
  lv_obj_center(suc_diag);
  lv_obj_add_style(suc_diag, &focal_style_card, 0);
  lv_obj_clear_flag(suc_diag, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tbox_suc = lv_obj_create(suc_diag);
  lv_obj_set_size(tbox_suc, 300, 30);
  lv_obj_align(tbox_suc, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(tbox_suc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(tbox_suc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(tbox_suc, 0, 0);
  lv_obj_set_flex_flow(tbox_suc, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tbox_suc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  focal_create_icon(tbox_suc, &focal_icon_check, FOCAL_HEX_PASTEL_MINT);
  lv_obj_t *lbl_suc_title = lv_label_create(tbox_suc);
  lv_label_set_text(lbl_suc_title, " Calibracion Completada");
  lv_obj_set_style_text_color(lbl_suc_title, FOCAL_COLOR_PASTEL_MINT, 0);
  s_calib_succ_title_lbl = lbl_suc_title;

  lv_obj_t *lbl_suc_msg = lv_label_create(suc_diag);
  lv_obj_set_width(lbl_suc_msg, 300);
  lv_label_set_long_mode(lbl_suc_msg, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_suc_msg, "La cama de corte ha sido nivelada correctamente.");
  lv_obj_set_style_text_align(lbl_suc_msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_suc_msg, LV_ALIGN_CENTER, 0, -5);
  s_calib_succ_desc_lbl = lbl_suc_msg;

  lv_obj_t *btn_suc_ok = focal_create_btn(suc_diag, 140, 38, &focal_icon_check, "Aceptar",
                                          &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_align(btn_suc_ok, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_add_event_cb(btn_suc_ok, on_calib_success_ok_cb, LV_EVENT_CLICKED, NULL);
  s_calib_succ_ok_lbl = lv_obj_get_child(btn_suc_ok, 1);

  update_calibration_bed_geometry();
  calib_update_view();
}

// =============================================================================
// SCREEN 5: CORTE RAPIDO CON CAMA INTERACTIVA (160x200 mm)
// =============================================================================
static int s_shape_x = 50;
static int s_shape_y = 50;
static int s_shape_dim = 40;
static int s_shape_power = 80;
static int s_shape_rotation = 0; // 0, 90, 180, 270 deg

static lv_obj_t *s_lbl_shape_pos = NULL;
static lv_obj_t *s_lbl_shape_size = NULL;
static lv_obj_t *s_lbl_shape_power = NULL;
static lv_point_t s_triangle_pts[4];

static void update_bed_shape_position(void) {
  if (!s_bed_obj) return;

  int w = s_shape_dim;
  int h = s_shape_dim;

  if (s_shape_type == 2) { // Rectangulo
    if (s_shape_rotation == 0 || s_shape_rotation == 180) {
      w = (s_shape_dim * 4) / 3;
      h = s_shape_dim;
    } else {
      w = s_shape_dim;
      h = (s_shape_dim * 4) / 3;
    }
  }

  // Strict physical bed boundary clamping in mm
  if (s_shape_x < 0) s_shape_x = 0;
  if (s_shape_x > s_bed_w_mm - w) s_shape_x = s_bed_w_mm - w;
  if (s_shape_y < 0) s_shape_y = 0;
  if (s_shape_y > s_bed_h_mm - h) s_shape_y = s_bed_h_mm - h;

  int bw = lv_obj_get_width(s_bed_obj);
  int bh = lv_obj_get_height(s_bed_obj);
  if (bw < 10) bw = 120;
  if (bh < 10) bh = 150;

  int eff_w = (s_bed_rot == 90 || s_bed_rot == 270) ? s_bed_h_mm : s_bed_w_mm;
  if (eff_w < 1) eff_w = 1;
  float scale = (float)bw / (float)eff_w;

  // Isotropic size on screen (preserving strict 1:1 pixel/mm ratio)
  int pw = (int)((s_bed_rot % 180 == 0 ? w : h) * scale);
  int ph = (int)((s_bed_rot % 180 == 0 ? h : w) * scale);
  if (pw < 4) pw = 4;
  if (ph < 4) ph = 4;

  // Center of shape in physical machine coordinates
  float cx_mm = (float)s_shape_x + (float)w * 0.5f;
  float cy_mm = (float)s_shape_y + (float)h * 0.5f;

  float ncx, ncy;
  focal_bed_calc_screen_coords(cx_mm, cy_mm, s_bed_w_mm, s_bed_h_mm, s_bed_rot, &ncx, &ncy);

  int px = (int)(ncx * (float)bw - (float)pw * 0.5f);
  int py = (int)(ncy * (float)bh - (float)ph * 0.5f);
  if (px < 0) px = 0;
  if (px + pw > bw) px = bw - pw;
  if (py < 0) py = 0;
  if (py + ph > bh) py = bh - ph;

  if (s_bed_shape_preview) {
    lv_obj_set_size(s_bed_shape_preview, pw, ph);
    lv_obj_set_pos(s_bed_shape_preview, px, py);

    if (s_shape_type == 3) {
      // Triangle: outline via vector line, transparent body
      lv_obj_set_style_bg_opa(s_bed_shape_preview, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(s_bed_shape_preview, 0, 0);
      if (s_bed_triangle_line) {
        lv_obj_clear_flag(s_bed_triangle_line, LV_OBJ_FLAG_HIDDEN);
        int rot = (s_shape_rotation + s_bed_rot) % 360;
        if (rot == 0) { // Apex UP
          s_triangle_pts[0] = (lv_point_t){(int16_t)(pw / 2), 0};
          s_triangle_pts[1] = (lv_point_t){(int16_t)pw, (int16_t)ph};
          s_triangle_pts[2] = (lv_point_t){0, (int16_t)ph};
          s_triangle_pts[3] = s_triangle_pts[0];
        } else if (rot == 90) { // Apex RIGHT
          s_triangle_pts[0] = (lv_point_t){(int16_t)pw, (int16_t)(ph / 2)};
          s_triangle_pts[1] = (lv_point_t){0, (int16_t)ph};
          s_triangle_pts[2] = (lv_point_t){0, 0};
          s_triangle_pts[3] = s_triangle_pts[0];
        } else if (rot == 180) { // Apex DOWN
          s_triangle_pts[0] = (lv_point_t){(int16_t)(pw / 2), (int16_t)ph};
          s_triangle_pts[1] = (lv_point_t){0, 0};
          s_triangle_pts[2] = (lv_point_t){(int16_t)pw, 0};
          s_triangle_pts[3] = s_triangle_pts[0];
        } else { // 270: Apex LEFT
          s_triangle_pts[0] = (lv_point_t){0, (int16_t)(ph / 2)};
          s_triangle_pts[1] = (lv_point_t){(int16_t)pw, 0};
          s_triangle_pts[2] = (lv_point_t){(int16_t)pw, (int16_t)ph};
          s_triangle_pts[3] = s_triangle_pts[0];
        }
        lv_line_set_points(s_bed_triangle_line, s_triangle_pts, 4);
      }
    } else {
      // Circle or Rectangle
      lv_obj_set_style_bg_opa(s_bed_shape_preview, LV_OPA_60, 0);
      lv_obj_set_style_border_width(s_bed_shape_preview, 1, 0);
      if (s_bed_triangle_line) {
        lv_obj_add_flag(s_bed_triangle_line, LV_OBJ_FLAG_HIDDEN);
      }
      if (s_shape_type == 0) {
        lv_obj_set_style_radius(s_bed_shape_preview, LV_RADIUS_CIRCLE, 0);
      } else {
        lv_obj_set_style_radius(s_bed_shape_preview, 2, 0);
      }
    }
  }

  if (s_lbl_shape_pos) {
    if (s_active_favorite_idx >= 0 && s_active_favorite_idx < s_num_favorites) {
      char buf[80];
      snprintf(buf, sizeof(buf), "Fav: %s", s_favorite_files[s_active_favorite_idx]);
      lv_label_set_text(s_lbl_shape_pos, buf);
    } else {
      char buf[32];
      snprintf(buf, sizeof(buf), "X: %d  Y: %d mm", s_shape_x, s_shape_y);
      lv_label_set_text(s_lbl_shape_pos, buf);
    }
  }
}

static void on_bed_touch_event_cb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev || !s_bed_obj)
    return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);

  lv_area_t bed_coords;
  lv_obj_get_coords(s_bed_obj, &bed_coords);

  int bw = lv_obj_get_width(s_bed_obj);
  int bh = lv_obj_get_height(s_bed_obj);
  if (bw <= 0 || bh <= 0) return;

  int rel_x = p.x - bed_coords.x1;
  int rel_y = p.y - bed_coords.y1;

  if (rel_x >= 0 && rel_x <= bw && rel_y >= 0 && rel_y <= bh) {
    float ntx = (float)rel_x / (float)bw;
    float nty = (float)rel_y / (float)bh;
    float nx, ny;
    switch (s_bed_rot) {
      case 90:
        nx = 1.0f - nty;
        ny = 1.0f - ntx;
        break;
      case 180:
        nx = 1.0f - ntx;
        ny = nty;
        break;
      case 270:
        nx = nty;
        ny = ntx;
        break;
      default: // 0
        nx = ntx;
        ny = 1.0f - nty;
        break;
    }
    float touch_x_mm = nx * (float)s_bed_w_mm;
    float touch_y_mm = ny * (float)s_bed_h_mm;

    int w = s_shape_dim;
    int h = s_shape_dim;
    if (s_shape_type == 2) {
      if (s_shape_rotation == 0 || s_shape_rotation == 180) {
        w = (s_shape_dim * 4) / 3;
        h = s_shape_dim;
      } else {
        w = s_shape_dim;
        h = (s_shape_dim * 4) / 3;
      }
    }

    s_shape_x = (int)(touch_x_mm - (float)w * 0.5f);
    s_shape_y = (int)(touch_y_mm - (float)h * 0.5f);
    update_bed_shape_position();
  }
}

static void on_shape_pos_preset_cb(lv_event_t *e) {
  int preset = (int)(uintptr_t)lv_event_get_user_data(e);
  int w = s_shape_dim;
  int h = s_shape_dim;
  if (s_shape_type == 2) {
    if (s_shape_rotation == 0 || s_shape_rotation == 180) {
      w = (s_shape_dim * 4) / 3;
      h = s_shape_dim;
    } else {
      w = s_shape_dim;
      h = (s_shape_dim * 4) / 3;
    }
  }

  if (preset == 0) { // Centro exacto
    s_shape_x = (s_bed_w_mm - w) / 2;
    s_shape_y = (s_bed_h_mm - h) / 2;
  } else if (preset == 1) { // Origen relativo (Home virtual fijado en Control de Ejes)
    s_shape_x = (int)s_vhome_x;
    s_shape_y = (int)s_vhome_y;
    if (s_shape_x + w > s_bed_w_mm) s_shape_x = s_bed_w_mm - w;
    if (s_shape_y + h > s_bed_h_mm) s_shape_y = s_bed_h_mm - h;
    if (s_shape_x < 0) s_shape_x = 0;
    if (s_shape_y < 0) s_shape_y = 0;
  }
  update_bed_shape_position();
}

void ui_shapes_set_preset(int preset_idx) {
  int w = s_shape_dim;
  int h = s_shape_dim;
  if (s_shape_type == 2) {
    if (s_shape_rotation == 0 || s_shape_rotation == 180) {
      w = (s_shape_dim * 4) / 3;
      h = s_shape_dim;
    } else {
      w = s_shape_dim;
      h = (s_shape_dim * 4) / 3;
    }
  }

  if (preset_idx == 0) { // Centro
    s_shape_x = (s_bed_w_mm - w) / 2;
    s_shape_y = (s_bed_h_mm - h) / 2;
  } else if (preset_idx == 1) { // Origen relativo (Home virtual)
    s_shape_x = (int)s_vhome_x;
    s_shape_y = (int)s_vhome_y;
    if (s_shape_x + w > s_bed_w_mm) s_shape_x = s_bed_w_mm - w;
    if (s_shape_y + h > s_bed_h_mm) s_shape_y = s_bed_h_mm - h;
    if (s_shape_x < 0) s_shape_x = 0;
    if (s_shape_y < 0) s_shape_y = 0;
  } else {
    s_shape_x = 40;
    s_shape_y = 40;
  }
  update_bed_shape_position();
}

void ui_shapes_rotate(int delta_deg) {
  s_shape_rotation = (s_shape_rotation + delta_deg) % 360;
  if (s_shape_rotation < 0) s_shape_rotation += 360;
  update_bed_shape_position();
}

static void on_shape_rotate_cb(lv_event_t *e) {
  int delta = (int)(intptr_t)lv_event_get_user_data(e);
  ui_shapes_rotate(delta);
}

void ui_shapes_nudge(int dx, int dy) {
  s_shape_x += dx;
  s_shape_y += dy;
  update_bed_shape_position();
}

static void on_shape_nudge_cb(lv_event_t *e) {
  int dir = (int)(uintptr_t)lv_event_get_user_data(e);
  int step = 5;
  int dx = 0, dy = 0;

  if (dir == 2) { // Visual Arriba / UP
    switch (s_bed_rot) {
      case 90:  dx =  step; break;
      case 180: dy = -step; break;
      case 270: dx = -step; break;
      default:  dy =  step; break;
    }
  } else if (dir == 3) { // Visual Abajo / DOWN
    switch (s_bed_rot) {
      case 90:  dx = -step; break;
      case 180: dy =  step; break;
      case 270: dx =  step; break;
      default:  dy = -step; break;
    }
  } else if (dir == 0) { // Visual Izq / LEFT
    switch (s_bed_rot) {
      case 90:  dy =  step; break;
      case 180: dx =  step; break;
      case 270: dy = -step; break;
      default:  dx = -step; break;
    }
  } else if (dir == 1) { // Visual Der / RIGHT
    switch (s_bed_rot) {
      case 90:  dy = -step; break;
      case 180: dx = -step; break;
      case 270: dy =  step; break;
      default:  dx =  step; break;
    }
  }

  s_shape_x += dx;
  s_shape_y += dy;
  update_bed_shape_position();
}

static void update_shapes_favorite_buttons(void) {
  for (int i = 0; i < 2; i++) {
    if (s_fav_btns[i]) {
      if (i < s_num_favorites && s_favorite_files[i][0] != '\0') {
        lv_obj_clear_flag(s_fav_btns[i], LV_OBJ_FLAG_HIDDEN);
        if (s_fav_labels[i]) {
          lv_label_set_text(s_fav_labels[i], s_favorite_files[i]);
        }
      } else {
        lv_obj_add_flag(s_fav_btns[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

void ui_shapes_select_favorite(int idx) {
  if (idx < 0 || idx >= s_num_favorites) return;
  s_active_favorite_idx = idx;

  // Unhighlight standard shape buttons
  for (int i = 0; i < 4; i++) {
    if (s_shape_sel_btns[i]) {
      lv_obj_set_style_bg_color(
          s_shape_sel_btns[i],
          s_is_dark ? lv_color_hex(0x262A35) : lv_color_hex(0xF0F2F5), 0);
      lv_obj_t *ic = lv_obj_get_child(s_shape_sel_btns[i], 0);
      if (ic) {
        lv_obj_set_style_img_recolor(
            ic, s_is_dark ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x111827), 0);
      }
    }
  }

  // Highlight selected favorite button
  for (int i = 0; i < 2; i++) {
    if (s_fav_btns[i]) {
      if (i == idx) {
        lv_obj_set_style_bg_color(s_fav_btns[i], focal_theme_get_accent_color(), 0);
      } else {
        lv_obj_set_style_bg_color(
            s_fav_btns[i],
            s_is_dark ? lv_color_hex(0x262A35) : lv_color_hex(0xF0F2F5), 0);
      }
    }
  }

  // Switch bed preview to G-Code silhouette
  if (s_bed_shape_preview) {
    lv_obj_add_flag(s_bed_shape_preview, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_shapes_bed_preview) {
    lv_obj_t *po = gcode_preview_get_obj(s_shapes_bed_preview);
    if (po) lv_obj_clear_flag(po, LV_OBJ_FLAG_HIDDEN);
    gcode_preview_set_theme(s_shapes_bed_preview, s_is_dark);
    gcode_preview_render_sync(s_shapes_bed_preview, s_favorite_files[idx], NULL, NULL);
  }

  if (s_lbl_shape_pos) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Fav: %s", s_favorite_files[idx]);
    lv_label_set_text(s_lbl_shape_pos, buf);
  }
}

void ui_shapes_select_shape(int idx) {
  if (idx < 0 || idx >= 4) return;
  s_shape_type = idx;
  s_active_favorite_idx = -1;

  // Unhighlight favorite buttons
  for (int i = 0; i < 2; i++) {
    if (s_fav_btns[i]) {
      lv_obj_set_style_bg_color(
          s_fav_btns[i],
          s_is_dark ? lv_color_hex(0x262A35) : lv_color_hex(0xF0F2F5), 0);
    }
  }

  // Switch bed preview to geometry shape preview
  if (s_shapes_bed_preview) {
    lv_obj_t *po = gcode_preview_get_obj(s_shapes_bed_preview);
    if (po) lv_obj_add_flag(po, LV_OBJ_FLAG_HIDDEN);
  }
  if (s_bed_shape_preview) {
    lv_obj_clear_flag(s_bed_shape_preview, LV_OBJ_FLAG_HIDDEN);
  }

  for (int i = 0; i < 4; i++) {
    if (s_shape_sel_btns[i]) {
      lv_obj_t *ic = lv_obj_get_child(s_shape_sel_btns[i], 0);
      if (i == idx) {
        lv_obj_set_style_bg_color(s_shape_sel_btns[i],
                                  focal_theme_get_accent_color(), 0);
        if (ic)
          lv_obj_set_style_img_recolor(ic, lv_color_hex(0xFFFFFF), 0);
      } else {
        lv_obj_set_style_bg_color(
            s_shape_sel_btns[i],
            s_is_dark ? lv_color_hex(0x262A35) : lv_color_hex(0xF0F2F5), 0);
        if (ic)
          lv_obj_set_style_img_recolor(
              ic, s_is_dark ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x111827),
              0);
      }
    }
  }
  update_bed_shape_position();
}

static void on_shape_icon_btn_click(lv_event_t *e) {
  int idx = (int)(uintptr_t)lv_event_get_user_data(e);
  ui_shapes_select_shape(idx);
}

static void on_shape_favorite_click_cb(lv_event_t *e) {
  int idx = (int)(uintptr_t)lv_event_get_user_data(e);
  ui_shapes_select_favorite(idx);
}

static void on_shape_size_adj_cb(lv_event_t *e) {
  int delta = (int)(intptr_t)lv_event_get_user_data(e);
  s_shape_dim += delta;
  if (s_shape_dim < 10)
    s_shape_dim = 10;
  if (s_shape_dim > 120)
    s_shape_dim = 120;
  if (s_lbl_shape_size) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Tamano: %d mm", s_shape_dim);
    lv_label_set_text(s_lbl_shape_size, buf);
  }
  update_bed_shape_position();
}

static void on_shape_power_adj_cb(lv_event_t *e) {
  int delta = (int)(intptr_t)lv_event_get_user_data(e);
  s_shape_power += delta;
  if (s_shape_power < 5)
    s_shape_power = 5;
  if (s_shape_power > 100)
    s_shape_power = 100;
  if (s_lbl_shape_power) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Potencia: %d%%", s_shape_power);
    lv_label_set_text(s_lbl_shape_power, buf);
  }
}

static void on_shape_action_cb(lv_event_t *e) {
  bool do_cut = (bool)(uintptr_t)lv_event_get_user_data(e);
  char cmd[64];
  char resp[32];

  if (s_active_favorite_idx >= 0 && s_active_favorite_idx < s_num_favorites) {
    // Mode: Favorite file cutting
    if (!do_cut) {
      laser_set_enable(true);
      laser_set_power(10); // 1%
      snprintf(cmd, sizeof(cmd), "G90 G0 X0 Y0 F3000");
      gcode_parser_execute_line(cmd, resp, sizeof(resp));
      snprintf(cmd, sizeof(cmd), "G1 X160 Y0 F2000");
      gcode_parser_execute_line(cmd, resp, sizeof(resp));
      snprintf(cmd, sizeof(cmd), "G1 X160 Y200");
      gcode_parser_execute_line(cmd, resp, sizeof(resp));
      snprintf(cmd, sizeof(cmd), "G1 X0 Y200");
      gcode_parser_execute_line(cmd, resp, sizeof(resp));
      snprintf(cmd, sizeof(cmd), "G1 X0 Y0");
      gcode_parser_execute_line(cmd, resp, sizeof(resp));
      laser_set_power(0);
      laser_set_enable(false);
    } else {
      ui_show_cut_modal(s_favorite_files[s_active_favorite_idx], 1200, 180);
    }
    return;
  }

  // Standard workshop shape mode
  int w = s_shape_dim;
  int h = s_shape_dim;
  if (s_shape_type == 2) {
    if (s_shape_rotation == 0 || s_shape_rotation == 180) {
      w = (s_shape_dim * 4) / 3;
      h = s_shape_dim;
    } else {
      w = s_shape_dim;
      h = (s_shape_dim * 4) / 3;
    }
  }

  if (!do_cut) {
    // Enmarcar con puntero al 1%
    laser_set_enable(true);
    laser_set_power(10); // 1%
    snprintf(cmd, sizeof(cmd), "G90 G0 X%d Y%d F3000", s_shape_x, s_shape_y);
    gcode_parser_execute_line(cmd, resp, sizeof(resp));
    snprintf(cmd, sizeof(cmd), "G1 X%d Y%d F2000", s_shape_x + w, s_shape_y);
    gcode_parser_execute_line(cmd, resp, sizeof(resp));
    snprintf(cmd, sizeof(cmd), "G1 X%d Y%d", s_shape_x + w, s_shape_y + h);
    gcode_parser_execute_line(cmd, resp, sizeof(resp));
    snprintf(cmd, sizeof(cmd), "G1 X%d Y%d", s_shape_x, s_shape_y + h);
    gcode_parser_execute_line(cmd, resp, sizeof(resp));
    snprintf(cmd, sizeof(cmd), "G1 X%d Y%d", s_shape_x, s_shape_y);
    gcode_parser_execute_line(cmd, resp, sizeof(resp));
    laser_set_power(0);
    laser_set_enable(false);
  } else {
    ui_show_cut_modal("corte_rapido.gcode", 140, 45);
  }
}

static void create_shapes_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  s_screens[UI_SCREEN_SHAPES] = scr;
  lv_obj_add_style(scr, &focal_style_screen, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Exact header title requested: "Corte Rapido"
  create_screen_header(UI_SCREEN_SHAPES, "Corte Rapido", true);

  // Left Panel: Simulated Machine Bed (160x200 mm) + Placement Controls
  lv_obj_t *card_bed = lv_obj_create(scr);
  lv_obj_set_size(card_bed, 196, 264);
  lv_obj_set_pos(card_bed, 10, 46);
  lv_obj_add_style(card_bed, &focal_style_card, 0);
  lv_obj_set_style_pad_all(card_bed, 0, 0);
  lv_obj_clear_flag(card_bed, LV_OBJ_FLAG_SCROLLABLE);

  // Bed Container 120x150 (Exact 0.8:1 aspect ratio matching 160x200mm)
  s_bed_obj = lv_obj_create(card_bed);
  lv_obj_set_size(s_bed_obj, 120, 150);
  lv_obj_align(s_bed_obj, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_set_style_bg_color(
      s_bed_obj, s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xE5E7EB),
      0);
  lv_obj_set_style_border_color(
      s_bed_obj, s_is_dark ? lv_color_hex(0x3B82F6) : lv_color_hex(0x2563EB),
      0);
  lv_obj_set_style_border_width(s_bed_obj, 1, 0);
  lv_obj_set_style_radius(s_bed_obj, 6, 0);
  lv_obj_set_style_pad_all(s_bed_obj, 0, 0);
  lv_obj_clear_flag(s_bed_obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_bed_obj, on_bed_touch_event_cb, LV_EVENT_PRESSING,
                      NULL);
  lv_obj_add_event_cb(s_bed_obj, on_bed_touch_event_cb, LV_EVENT_PRESSED, NULL);

  // Shape preview on bed
  s_bed_shape_preview = lv_obj_create(s_bed_obj);
  lv_obj_set_style_bg_color(s_bed_shape_preview, focal_theme_get_accent_color(),
                            0);
  lv_obj_set_style_bg_opa(s_bed_shape_preview, LV_OPA_60, 0);
  lv_obj_set_style_border_color(s_bed_shape_preview, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(s_bed_shape_preview, 1, 0);
  lv_obj_set_style_pad_all(s_bed_shape_preview, 0, 0);
  lv_obj_clear_flag(s_bed_shape_preview,
                    LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  // Triangle line inside shape preview
  s_bed_triangle_line = lv_line_create(s_bed_shape_preview);
  lv_obj_set_style_line_color(s_bed_triangle_line, focal_theme_get_accent_color(), 0);
  lv_obj_set_style_line_width(s_bed_triangle_line, 2, 0);
  lv_obj_set_style_line_rounded(s_bed_triangle_line, true, 0);
  lv_obj_add_flag(s_bed_triangle_line, LV_OBJ_FLAG_HIDDEN);

  // 2D G-Code favorite silhouette preview on bed (hidden by default)
  s_shapes_bed_preview = gcode_preview_create(s_bed_obj, 120, 150);
  lv_obj_t *prev_root = gcode_preview_get_obj(s_shapes_bed_preview);
  if (prev_root) {
    lv_obj_set_pos(prev_root, 0, 0);
    lv_obj_add_flag(prev_root, LV_OBJ_FLAG_HIDDEN);
  }

  // Row 1 (Presets): "Centro" & "Origen" (wide 86px buttons, text never touches borders)
  lv_obj_t *btn_cen = focal_create_btn(card_bed, 86, 24, NULL, "Centro",
                                       &focal_style_btn_secondary, 0);
  lv_obj_set_pos(btn_cen, 8, 158);
  lv_obj_set_ext_click_area(btn_cen, 6);
  lv_obj_add_event_cb(btn_cen, on_shape_pos_preset_cb, LV_EVENT_CLICKED,
                      (void *)0);
  s_shp_btn_center_lbl = lv_obj_get_child(btn_cen, 0);

  lv_obj_t *btn_ori = focal_create_btn(card_bed, 86, 24, NULL, "Origen",
                                       &focal_style_btn_secondary, 0);
  lv_obj_set_pos(btn_ori, 102, 158);
  lv_obj_set_ext_click_area(btn_ori, 6);
  lv_obj_add_event_cb(btn_ori, on_shape_pos_preset_cb, LV_EVENT_CLICKED,
                      (void *)1);
  s_shp_btn_origin_lbl = lv_obj_get_child(btn_ori, 0);

  // Row 2 (Rotation): Rotate CCW (↺) and Rotate CW (↻) with vector icons only
  lv_obj_t *btn_r_ccw = focal_create_btn(card_bed, 86, 24, &focal_icon_rot_ccw, NULL,
                                         &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_set_pos(btn_r_ccw, 8, 186);
  lv_obj_set_ext_click_area(btn_r_ccw, 6);
  lv_obj_add_event_cb(btn_r_ccw, on_shape_rotate_cb, LV_EVENT_CLICKED,
                      (void *)(intptr_t)-90);

  lv_obj_t *btn_r_cw = focal_create_btn(card_bed, 86, 24, &focal_icon_rot_cw, NULL,
                                        &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_set_pos(btn_r_cw, 102, 186);
  lv_obj_set_ext_click_area(btn_r_cw, 6);
  lv_obj_add_event_cb(btn_r_cw, on_shape_rotate_cb, LV_EVENT_CLICKED,
                      (void *)(intptr_t)90);

  // Row 3 (Nudge): 4 Micro-step Buttons (-5 / +5 mm)
  const lv_img_dsc_t *nudge_icons[4] = {
      &focal_icon_jog_left, &focal_icon_jog_right, &focal_icon_jog_up,
      &focal_icon_jog_down};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *bn =
        focal_create_btn(card_bed, 38, 24, nudge_icons[i], NULL,
                         &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
    lv_obj_set_pos(bn, 10 + i * 46, 214);
    lv_obj_set_ext_click_area(bn, 6);
    lv_obj_add_event_cb(bn, on_shape_nudge_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
  }

  // Row 4: Coordinates Readout
  s_lbl_shape_pos = lv_label_create(card_bed);
  lv_label_set_text(s_lbl_shape_pos, "X: 50  Y: 50 mm");
  lv_obj_set_style_text_color(s_lbl_shape_pos, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_size(s_lbl_shape_pos, 180, 18);
  lv_label_set_long_mode(s_lbl_shape_pos, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_lbl_shape_pos, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_lbl_shape_pos, LV_ALIGN_BOTTOM_MID, 0, -4);

  update_bed_shape_position();

  // Right Panel: Shape selector, Incremental Steppers (+/- 1mm, +/- 5%), Favorites, Actions
  lv_obj_t *card_ctrl = lv_obj_create(scr);
  lv_obj_set_size(card_ctrl, 254, 264);
  lv_obj_set_pos(card_ctrl, 216, 46);
  lv_obj_add_style(card_ctrl, &focal_style_card, 0);
  lv_obj_set_style_pad_all(card_ctrl, 8, 0);
  lv_obj_clear_flag(card_ctrl, LV_OBJ_FLAG_SCROLLABLE);

  // Row 1: 4 Workshop Shapes (Circle, Square, Rectangle, Triangle)
  const lv_img_dsc_t *shape_icons[4] = {
      &focal_icon_shape_circle,
      &focal_icon_shape_square,
      &focal_icon_shape_rect,
      &focal_icon_shape_triangle
  };

  for (int i = 0; i < 4; i++) {
    lv_obj_t *b = lv_btn_create(card_ctrl);
    lv_obj_set_size(b, 54, 32);
    lv_obj_set_pos(b, 2 + i * 60, 4);
    lv_obj_add_style(b, &focal_style_btn_secondary, 0);
    lv_obj_set_style_radius(b, FOCAL_RADIUS_S, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    s_shape_sel_btns[i] = b;

    uint32_t icon_col = (i == 0) ? 0xFFFFFF : (s_is_dark ? 0xFFFFFF : 0x111827);
    lv_obj_t *ic = focal_create_icon(b, shape_icons[i], icon_col);
    lv_obj_center(ic);
    if (i == 0) {
      lv_obj_set_style_bg_color(b, focal_theme_get_accent_color(), 0);
    }
    lv_obj_add_event_cb(b, on_shape_icon_btn_click, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
  }

  // Row 2: Size Label & +/- Steppers (Step of 1 mm)
  s_lbl_shape_size = lv_label_create(card_ctrl);
  lv_label_set_text(s_lbl_shape_size, "Tamano: 40 mm");
  lv_obj_align(s_lbl_shape_size, LV_ALIGN_TOP_LEFT, 4, 46);

  lv_obj_t *bs_m = focal_create_btn(card_ctrl, 30, 24, NULL, "-",
                                    &focal_style_btn_secondary, 0);
  lv_obj_set_pos(bs_m, 168, 42);
  lv_obj_set_ext_click_area(bs_m, 8);
  lv_obj_add_event_cb(bs_m, on_shape_size_adj_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

  lv_obj_t *bs_p = focal_create_btn(card_ctrl, 30, 24, NULL, "+",
                                    &focal_style_btn_secondary, 0);
  lv_obj_set_pos(bs_p, 204, 42);
  lv_obj_set_ext_click_area(bs_p, 8);
  lv_obj_add_event_cb(bs_p, on_shape_size_adj_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

  // Row 3: Power Label & +/- Steppers (Step of 5%)
  s_lbl_shape_power = lv_label_create(card_ctrl);
  lv_label_set_text(s_lbl_shape_power, "Potencia: 80%");
  lv_obj_align(s_lbl_shape_power, LV_ALIGN_TOP_LEFT, 4, 76);

  lv_obj_t *bp_m = focal_create_btn(card_ctrl, 30, 24, NULL, "-",
                                    &focal_style_btn_secondary, 0);
  lv_obj_set_pos(bp_m, 168, 72);
  lv_obj_set_ext_click_area(bp_m, 8);
  lv_obj_add_event_cb(bp_m, on_shape_power_adj_cb, LV_EVENT_CLICKED,
                      (void *)(intptr_t)-5);

  lv_obj_t *bp_p = focal_create_btn(card_ctrl, 30, 24, NULL, "+",
                                    &focal_style_btn_secondary, 0);
  lv_obj_set_pos(bp_p, 204, 72);
  lv_obj_set_ext_click_area(bp_p, 8);
  lv_obj_add_event_cb(bp_p, on_shape_power_adj_cb, LV_EVENT_CLICKED,
                      (void *)(intptr_t)5);

  // Two Favorite List Buttons with single-line ellipsis (38px height)
  for (int i = 0; i < 2; i++) {
    lv_obj_t *bf = focal_create_btn(card_ctrl, 238, 38, &focal_icon_star,
                                    s_favorite_files[i],
                                    &focal_style_btn_secondary,
                                    FOCAL_HEX_PASTEL_AMBER);
    lv_obj_set_pos(bf, 0, 106 + i * 44);
    lv_obj_set_flex_align(bf, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bf, 8, 0);
    lv_obj_set_style_pad_right(bf, 8, 0);
    lv_obj_set_ext_click_area(bf, 4);

    lv_obj_t *lbl = lv_obj_get_child(bf, 1);
    if (!lbl) {
      lbl = lv_label_create(bf);
      lv_obj_set_style_text_font(lbl, FOCAL_FONT, 0);
      lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_flex_grow(lbl, 1);
    }
    lv_obj_set_size(lbl, 192, 18);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    if (s_favorite_files[i][0] != '\0') {
      lv_label_set_text(lbl, s_favorite_files[i]);
    } else {
      lv_label_set_text(lbl, "");
    }
    s_fav_labels[i] = lbl;
    s_fav_btns[i] = bf;
    lv_obj_add_event_cb(bf, on_shape_favorite_click_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
    if (i >= s_num_favorites || s_favorite_files[i][0] == '\0') {
      lv_obj_add_flag(bf, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Row 7: Action Buttons: Enmarcar & Cortar (zero collision, calibrated 12px gap)
  lv_obj_t *btn_fb =
      focal_create_btn(card_ctrl, 110, 42, &focal_icon_laser, "Enmarcar",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_AMBER);
  lv_obj_align(btn_fb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_ext_click_area(btn_fb, 6);
  lv_obj_add_event_cb(btn_fb, on_shape_action_cb, LV_EVENT_CLICKED,
                       (void *)false);
  s_shp_btn_frame_lbl = lv_obj_get_child(btn_fb, 1);

  lv_obj_t *btn_cb =
      focal_create_btn(card_ctrl, 116, 42, &focal_icon_play, "Cortar",
                       &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_align(btn_cb, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_pad_left(btn_cb, 8, 0);
  lv_obj_set_style_pad_right(btn_cb, 8, 0);
  lv_obj_set_ext_click_area(btn_cb, 6);
  lv_obj_add_event_cb(btn_cb, on_shape_action_cb, LV_EVENT_CLICKED,
                       (void *)true);
  s_shp_btn_cut_lbl = lv_obj_get_child(btn_cb, 1);

  update_shapes_bed_geometry();
}

// =============================================================================
// SCREEN 6: CONFIGURACION DE MAQUINA (MASTER-DETAIL LIST LAYOUT)
// =============================================================================

// Wi-Fi Input Modal Overlay (Input box top + Keyboard bottom)
static lv_obj_t *s_modal_ssid_lbl = NULL;
static lv_obj_t *s_modal_pass_ta = NULL;
static char s_connecting_ssid[33] = {0};

static void show_wifi_input_modal(const char *ssid) {
  strncpy(s_connecting_ssid, ssid, sizeof(s_connecting_ssid) - 1);
  if (s_modal_ssid_lbl) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Red: %s", s_connecting_ssid);
    lv_label_set_text(s_modal_ssid_lbl, buf);
  }
  if (s_modal_pass_ta) {
    lv_textarea_set_text(s_modal_pass_ta, "");
  }
  if (s_wifi_modal) {
    lv_obj_clear_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_wifi_modal);
  }
}

static void hide_wifi_input_modal(void) {
  if (s_wifi_modal) {
    lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_show_wifi_modal(const char *ssid) {
  show_wifi_input_modal(ssid ? ssid : "Mi_Red_Taller");
}

void ui_hide_wifi_modal(void) { hide_wifi_input_modal(); }

static void on_modal_connect_cb(lv_event_t *e) {
  const char *pass = lv_textarea_get_text(s_modal_pass_ta);
  hide_wifi_input_modal();

  machine_settings_t cfg;
  settings_manager_get(&cfg);
  strncpy(cfg.wifi_ssid, s_connecting_ssid, sizeof(cfg.wifi_ssid) - 1);
  strncpy(cfg.wifi_pass, pass, sizeof(cfg.wifi_pass) - 1);
  cfg.wifi_sta_enabled = true;
  settings_manager_save(&cfg);

  settings_manager_add_or_update_wifi(s_connecting_ssid, pass);

  wifi_manager_connect_sta(s_connecting_ssid, pass);
  ui_update_wifi_display();

  if (s_lbl_wifi_detail) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Conectando a '%s'...", s_connecting_ssid);
    lv_label_set_text(s_lbl_wifi_detail, buf);
  }
}

static void on_modal_cancel_cb(lv_event_t *e) { hide_wifi_input_modal(); }

static void on_wifi_item_click_cb(lv_event_t *e) {
  const char *ssid = (const char *)lv_event_get_user_data(e);
  if (!ssid) return;

  char saved_pass[64] = {0};
  if (settings_manager_find_wifi(ssid, saved_pass, sizeof(saved_pass))) {
    machine_settings_t cfg;
    settings_manager_get(&cfg);
    strncpy(cfg.wifi_ssid, ssid, sizeof(cfg.wifi_ssid) - 1);
    strncpy(cfg.wifi_pass, saved_pass, sizeof(cfg.wifi_pass) - 1);
    cfg.wifi_sta_enabled = true;
    settings_manager_save(&cfg);

    wifi_manager_connect_sta(ssid, saved_pass);
    ui_update_wifi_display();

    if (s_lbl_wifi_detail) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Conectando a '%s'...", ssid);
      lv_label_set_text(s_lbl_wifi_detail, buf);
    }
  } else {
    show_wifi_input_modal(ssid);
  }
}

// Background scan task
static void wifi_scan_task_fn(void *pv) {
  wifi_scan_item_t scan_list[WIFI_MAX_SCAN_AP];
  uint16_t ap_count = 0;
  esp_err_t err = wifi_manager_scan(scan_list, WIFI_MAX_SCAN_AP, &ap_count);

  if (lvgl_port_lock(1000)) {
    if (s_btn_wifi_scan) {
      lv_obj_t *lbl = lv_obj_get_child(s_btn_wifi_scan, 1);
      if (lbl)
        lv_label_set_text(lbl, "Escanear");
    }
    if (s_wifi_list) {
      lv_obj_clean(s_wifi_list);
      if (err == ESP_OK && ap_count > 0) {
        for (int i = 0; i < ap_count; i++) {
          const lv_img_dsc_t *w_icon = focal_get_wifi_icon_by_rssi(scan_list[i].rssi);
          lv_obj_t *btn = focal_create_btn(
              s_wifi_list, 286, 38, w_icon, scan_list[i].ssid,
              &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
          lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                LV_FLEX_ALIGN_CENTER);
          lv_obj_set_style_pad_left(btn, 10, 0);
          lv_obj_set_style_pad_column(btn, 10, 0);

          if (wifi_manager_is_sta_connected() &&
              strcmp(wifi_manager_get_sta_ssid(), scan_list[i].ssid) == 0) {
            lv_obj_set_style_border_color(btn, FOCAL_COLOR_PASTEL_MINT, 0);
            lv_obj_set_style_border_width(btn, 1, 0);
            lv_obj_t *ic = lv_obj_get_child(btn, 0);
            if (ic) lv_obj_set_style_img_recolor(ic, FOCAL_COLOR_PASTEL_MINT, 0);
          }

          lv_obj_add_event_cb(btn, on_wifi_item_click_cb, LV_EVENT_CLICKED,
                              (void *)strdup(scan_list[i].ssid));
        }
        if (s_lbl_wifi_detail) {
          char st[64];
          snprintf(st, sizeof(st), "%u redes encontradas.", ap_count);
          lv_label_set_text(s_lbl_wifi_detail, st);
        }
      } else {
        lv_obj_t *lbl = lv_label_create(s_wifi_list);
        lv_label_set_text(lbl, "No se detectaron redes.");
        lv_obj_set_style_text_color(
            lbl, s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x4B5563), 0);
      }
    }
    lvgl_port_unlock();
  }
  vTaskDelete(NULL);
}

static void on_wifi_scan_btn_cb(lv_event_t *e) {
  if (s_btn_wifi_scan) {
    lv_obj_t *lbl = lv_obj_get_child(s_btn_wifi_scan, 1);
    if (lbl)
      lv_label_set_text(lbl, "Buscando...");
  }
  if (s_lbl_wifi_detail) {
    lv_label_set_text(s_lbl_wifi_detail, "Escaneando canales 2.4 GHz...");
  }
  xTaskCreate(wifi_scan_task_fn, "wscan", 4096, NULL, 3, NULL);
}

// Master Menu Selection Handler
void ui_settings_select_tab(int idx) {
  if (idx < 0 || idx >= 6)
    return;
  s_settings_tab = idx;

  for (int i = 0; i < 6; i++) {
    if (s_settings_menu_btns[i]) {
      lv_obj_t *ic = lv_obj_get_child(s_settings_menu_btns[i], 0);
      lv_obj_t *lbl = lv_obj_get_child(s_settings_menu_btns[i], 1);
      if (i == idx) {
        lv_obj_set_style_bg_color(s_settings_menu_btns[i],
                                  focal_theme_get_accent_color(), 0);
        lv_obj_set_style_text_color(s_settings_menu_btns[i],
                                    lv_color_hex(0xFFFFFF), 0);
        if (ic) lv_obj_set_style_img_recolor(ic, lv_color_hex(0xFFFFFF), 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
      } else {
        lv_obj_set_style_bg_color(
            s_settings_menu_btns[i],
            s_is_dark ? lv_color_hex(0x1A1D24) : lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(
            s_settings_menu_btns[i],
            s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
        if (ic) lv_obj_set_style_img_recolor(
            ic, s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
        if (lbl) lv_obj_set_style_text_color(
            lbl, s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
      }
    }
    if (s_settings_panels[i]) {
      if (i == idx) {
        lv_obj_clear_flag(s_settings_panels[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(s_settings_panels[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

static void on_settings_menu_select_cb(lv_event_t *e) {
  int idx = (int)(uintptr_t)lv_event_get_user_data(e);
  ui_settings_select_tab(idx);
}

// Appearance Handlers
static void on_theme_toggle_cb(lv_event_t *e) {
  bool is_dark = (bool)(uintptr_t)lv_event_get_user_data(e);
  focal_theme_set_dark(is_dark);
}

static void on_accent_select_cb(lv_event_t *e) {
  uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  focal_theme_set_accent(idx);
}

// Confirmation Dialog Modal Helper
static void show_settings_confirm_dialog(const char *title, const char *msg,
                                        const char *confirm_txt, bool is_danger,
                                        void (*action_cb)(void)) {
  s_confirm_action_cb = action_cb;
  if (s_confirm_title_lbl) {
    lv_label_set_text(s_confirm_title_lbl, title);
    lv_obj_set_style_text_color(
        s_confirm_title_lbl,
        is_danger ? FOCAL_COLOR_PASTEL_RED : focal_theme_get_accent_color(), 0);
  }
  if (s_confirm_msg_lbl) {
    lv_label_set_text(s_confirm_msg_lbl, msg);
  }
  if (s_confirm_btn_ok) {
    lv_obj_t *lbl = lv_obj_get_child(s_confirm_btn_ok, 1);
    if (lbl) lv_label_set_text(lbl, confirm_txt);
    lv_obj_remove_style(s_confirm_btn_ok, &focal_style_btn_danger, 0);
    lv_obj_remove_style(s_confirm_btn_ok, &focal_style_btn_mint, 0);
    lv_obj_add_style(s_confirm_btn_ok,
                     is_danger ? &focal_style_btn_danger : &focal_style_btn_mint,
                     0);
  }
  if (s_confirm_icon) {
    lv_img_set_src(s_confirm_icon, is_danger ? &focal_icon_trash : &focal_icon_check);
    lv_obj_set_style_img_recolor(
        s_confirm_icon,
        is_danger ? FOCAL_COLOR_PASTEL_RED : FOCAL_COLOR_PASTEL_MINT, 0);
  }
  if (s_settings_confirm_modal) {
    lv_obj_clear_flag(s_settings_confirm_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_settings_confirm_modal);
  }
}

static void hide_settings_confirm_dialog(void) {
  if (s_settings_confirm_modal) {
    lv_obj_add_flag(s_settings_confirm_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

static void on_confirm_modal_cancel_cb(lv_event_t *e) {
  hide_settings_confirm_dialog();
}

static void on_confirm_modal_ok_cb(lv_event_t *e) {
  hide_settings_confirm_dialog();
  if (s_confirm_action_cb) {
    s_confirm_action_cb();
  }
}

// Action Callbacks for Confirmation Modals
static void do_wifi_forget_action(void) {
  settings_manager_forget_current_wifi();
  wifi_manager_disconnect_sta();
  ui_update_wifi_display();

  if (s_lbl_wifi_detail) {
    lv_label_set_text(s_lbl_wifi_detail, "Desconectado");
    lv_obj_set_style_text_color(
        s_lbl_wifi_detail,
        s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x4B5563), 0);
  }
  if (s_wifi_detail_icon) {
    lv_img_set_src(s_wifi_detail_icon, &focal_icon_wifi_dot);
    lv_obj_set_style_img_recolor(s_wifi_detail_icon, lv_color_hex(0x9CA3AF), 0);
  }
}

static void on_wifi_forget_btn_cb(lv_event_t *e) {
  if (!wifi_manager_is_sta_connected()) {
    return;
  }
  char msg[128];
  snprintf(msg, sizeof(msg),
           "Deseas olvidar la red '%s'?\nTendras que ingresar la clave nuevamente.",
           wifi_manager_get_sta_ssid());
  show_settings_confirm_dialog("Olvidar Red Wi-Fi", msg, "Olvidar", true,
                               do_wifi_forget_action);
}

static void do_laser_save(void) {
  machine_settings_t cfg;
  settings_manager_get(&cfg);
  cfg.laser_max_s = (uint16_t)(s_laser_power_val * 10);
  settings_manager_save(&cfg);
}

static void on_save_laser_btn_cb(lv_event_t *e) {
  show_settings_confirm_dialog(
      "Parametros Laser",
      "Deseas guardar la nueva potencia maxima del laser?",
      "Guardar",
      false,
      do_laser_save);
}

static void on_laser_slider_change_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  s_laser_power_val = (int)lv_slider_get_value(slider);
  if (s_lbl_laser_pwr) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Potencia Maxima: %d%%", s_laser_power_val);
    lv_label_set_text(s_lbl_laser_pwr, buf);
  }
}

static void on_laser_pulse_btn_cb(lv_event_t *e) {
  laser_set_enable(true);
  laser_set_power((uint32_t)(s_laser_power_val * 10));
  vTaskDelay(pdMS_TO_TICKS(100));
  laser_set_power(0);
  laser_set_enable(false);
}

static void do_sd_format(void) {
  ESP_LOGW(TAG, "Formateando MicroSD...");
#if defined(ESP_PLATFORM)
  sd_card_format();
#endif
}

static void on_sd_fmt_btn_cb(lv_event_t *e) {
  show_settings_confirm_dialog(
      "Formatear SD",
      "Deseas formatear la tarjeta MicroSD?\nSe eliminaran permanentemente todos los archivos.",
      "Formatear",
      true,
      do_sd_format);
}

static void do_kinematics_save(void) {
  machine_settings_t cfg;
  settings_manager_get(&cfg);
  if (s_slider_kin_feed) {
    cfg.max_rate_x = (float)lv_slider_get_value(s_slider_kin_feed);
    cfg.max_rate_y = cfg.max_rate_x;
  }
  cfg.travel_max_x = (float)s_bed_w_mm;
  cfg.travel_max_y = (float)s_bed_h_mm;
  cfg.bed_rotation = (int16_t)s_bed_rot;
  settings_manager_save(&cfg);

  char cmd[64], resp[64];
  snprintf(cmd, sizeof(cmd), "$110=%.0f", cfg.max_rate_x);
  gcode_parser_execute_line(cmd, resp, sizeof(resp));
  snprintf(cmd, sizeof(cmd), "$111=%.0f", cfg.max_rate_y);
  gcode_parser_execute_line(cmd, resp, sizeof(resp));
}

static void on_save_kin_btn_cb(lv_event_t *e) {
  show_settings_confirm_dialog(
      "Cinematica de Maquina",
      "Guardar velocidad y limites de carrera configurados?",
      "Guardar",
      false,
      do_kinematics_save);
}

static void on_kin_feed_slider_change_cb(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  int feed = (int)lv_slider_get_value(slider);
  if (s_lbl_kin_feed) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Velocidad Max: %d mm/min", feed);
    lv_label_set_text(s_lbl_kin_feed, buf);
  }
}

static void on_kin_rot_click_cb(lv_event_t *e) {
  s_bed_rot = (s_bed_rot + 90) % 360;
  if (s_lbl_kin_bed_info) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Cama: %d x %d mm  |  Rot: %d°", s_bed_w_mm, s_bed_h_mm, s_bed_rot);
    lv_label_set_text(s_lbl_kin_bed_info, buf);
  }
  if (s_btn_kin_rot) {
    lv_obj_t *lbl = lv_obj_get_child(s_btn_kin_rot, 1);
    if (lbl) lv_label_set_text(lbl, "Rotar");
  }
  machine_settings_t cfg;
  settings_manager_get(&cfg);
  cfg.bed_rotation = (int16_t)s_bed_rot;
  settings_manager_save(&cfg);
}

static void on_kin_edit_dim_click_cb(lv_event_t *e) {
  s_edit_bed_w = s_bed_w_mm;
  s_edit_bed_h = s_bed_h_mm;
  s_edit_active_field = 0;
  snprintf(s_keypad_buf, sizeof(s_keypad_buf), "%d", s_edit_bed_w);
  update_bed_dimensions_preview();
  ui_switch_screen(UI_SCREEN_BED_DIMENSIONS);
}

static void do_factory_reset(void) {
  settings_manager_reset_defaults();
  focal_theme_init();
  ui_switch_screen(UI_SCREEN_LAUNCHER);
}

static void on_reset_btn_cb(lv_event_t *e) {
  show_settings_confirm_dialog(
      "Restablecer de Fabrica",
      "Restablecer todas las configuraciones a valores de fabrica?\nSe borraran redes Wi-Fi y calibraciones.",
      "Restablecer",
      true,
      do_factory_reset);
}

// Legal Notice Modal Handlers
void ui_show_legal_modal(void) {
  if (s_legal_modal) {
    lv_obj_clear_flag(s_legal_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_legal_modal);
  }
}

void ui_hide_legal_modal(void) {
  if (s_legal_modal) {
    lv_obj_add_flag(s_legal_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

static void on_legal_modal_close_cb(lv_event_t *e) {
  ui_hide_legal_modal();
}

static void on_legal_btn_cb(lv_event_t *e) {
  ui_show_legal_modal();
}

static void create_settings_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  s_screens[UI_SCREEN_SETTINGS] = scr;
  lv_obj_add_style(scr, &focal_style_screen, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_screen_header(UI_SCREEN_SETTINGS, "Ajustes de Maquina", true);

  // Master List (Left Column, Width 136, Height 266)
  lv_obj_t *nav_card = lv_obj_create(scr);
  lv_obj_set_size(nav_card, 136, 266);
  lv_obj_set_pos(nav_card, 6, 46);
  lv_obj_add_style(nav_card, &focal_style_card, 0);
  lv_obj_set_style_pad_all(nav_card, 0, 0);
  lv_obj_clear_flag(nav_card, LV_OBJ_FLAG_SCROLLABLE);

  const char *cat_names[6] = {"Red Wi-Fi",  "Apariencia", "Laser",
                              "Tarjeta SD", "Cinematica", "Sistema"};
  const lv_img_dsc_t *cat_icons[6] = {&focal_icon_wifi,   &focal_icon_theme,
                                      &focal_icon_laser,  &focal_icon_files,
                                      &focal_icon_shapes, &focal_icon_settings};

  for (int i = 0; i < 6; i++) {
    lv_obj_t *btn =
        focal_create_btn(nav_card, 126, 38, cat_icons[i], cat_names[i],
                         &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
    lv_obj_set_pos(btn, 5, 6 + i * 42);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(btn, 6, 0);
    lv_obj_set_style_pad_column(btn, 4, 0);
    s_settings_menu_btns[i] = btn;

    lv_obj_t *ic = lv_obj_get_child(btn, 0);
    lv_obj_t *lbl = lv_obj_get_child(btn, 1);
    s_settings_menu_lbls[i] = lbl;
    if (i == 0) {
      lv_obj_set_style_bg_color(btn, focal_theme_get_accent_color(), 0);
      lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
      if (ic) lv_obj_set_style_img_recolor(ic, lv_color_hex(0xFFFFFF), 0);
      if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    } else {
      if (ic) lv_obj_set_style_img_recolor(
          ic, s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
      if (lbl) lv_obj_set_style_text_color(
          lbl, s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);
    }
    lv_obj_add_event_cb(btn, on_settings_menu_select_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
  }

  // Detail Area (Right Column, Width 328, Height 266)
  lv_obj_t *detail_area = lv_obj_create(scr);
  lv_obj_set_size(detail_area, 328, 266);
  lv_obj_set_pos(detail_area, 146, 46);
  lv_obj_add_style(detail_area, &focal_style_card, 0);
  lv_obj_set_style_pad_all(detail_area, 0, 0);
  lv_obj_clear_flag(detail_area, LV_OBJ_FLAG_SCROLLABLE);

  // ==========================================
  // DETAIL PANEL 0: RED WI-FI
  // ==========================================
  lv_obj_t *p0 = lv_obj_create(detail_area);
  lv_obj_set_size(p0, 328, 266);
  lv_obj_set_pos(p0, 0, 0);
  lv_obj_clear_flag(p0, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(p0, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p0, 0, 0);
  lv_obj_set_style_pad_all(p0, 0, 0);
  lv_obj_set_style_text_color(p0, lv_color_hex(s_is_dark ? 0xF9FAFB : 0x111827), 0);
  s_settings_panels[0] = p0;

  s_btn_wifi_scan =
      focal_create_btn(p0, 146, 36, &focal_icon_refresh, "Escanear",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_set_pos(s_btn_wifi_scan, 12, 10);
  lv_obj_set_ext_click_area(s_btn_wifi_scan, 6);
  lv_obj_add_event_cb(s_btn_wifi_scan, on_wifi_scan_btn_cb, LV_EVENT_CLICKED,
                      NULL);
  s_wifi_btn_scan_lbl = lv_obj_get_child(s_btn_wifi_scan, 1);

  lv_obj_t *btn_forget =
      focal_create_btn(p0, 146, 36, &focal_icon_trash, "Olvidar Red",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_RED);
  lv_obj_set_pos(btn_forget, 170, 10);
  lv_obj_set_ext_click_area(btn_forget, 6);
  lv_obj_add_event_cb(btn_forget, on_wifi_forget_btn_cb, LV_EVENT_CLICKED,
                      NULL);
  s_wifi_btn_forget_lbl = lv_obj_get_child(btn_forget, 1);

  // Permanent Connected / Status Card at y=52 (304x38 px)
  s_wifi_cur_card = lv_obj_create(p0);
  lv_obj_set_size(s_wifi_cur_card, 304, 38);
  lv_obj_set_pos(s_wifi_cur_card, 12, 52);
  lv_obj_add_style(s_wifi_cur_card, &focal_style_btn_secondary, 0);
  lv_obj_set_style_radius(s_wifi_cur_card, FOCAL_RADIUS_M, 0);
  lv_obj_set_style_pad_all(s_wifi_cur_card, 4, 0);
  lv_obj_set_flex_flow(s_wifi_cur_card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_wifi_cur_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_wifi_cur_card, 6, 0);
  lv_obj_clear_flag(s_wifi_cur_card, LV_OBJ_FLAG_SCROLLABLE);

  s_wifi_detail_icon = focal_create_icon(
      s_wifi_cur_card, &focal_icon_wifi_dot, 0x9CA3AF);

  s_lbl_wifi_detail_ssid = lv_label_create(s_wifi_cur_card);
  lv_label_set_text(s_lbl_wifi_detail_ssid, "Sin Wi-Fi");
  lv_obj_set_style_text_color(s_lbl_wifi_detail_ssid, lv_color_hex(0x9CA3AF), 0);
  lv_obj_set_style_text_font(s_lbl_wifi_detail_ssid, FOCAL_FONT, 0);
  lv_obj_set_width(s_lbl_wifi_detail_ssid, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(s_lbl_wifi_detail_ssid, 120, 0);
  lv_label_set_long_mode(s_lbl_wifi_detail_ssid, LV_LABEL_LONG_DOT);

  s_lbl_wifi_detail_sep = lv_label_create(s_wifi_cur_card);
  lv_label_set_text(s_lbl_wifi_detail_sep, "-");
  lv_obj_set_style_text_color(
      s_lbl_wifi_detail_sep, s_is_dark ? lv_color_hex(0x6B7280) : lv_color_hex(0x9CA3AF), 0);
  lv_obj_set_style_text_font(s_lbl_wifi_detail_sep, FOCAL_FONT, 0);

  s_lbl_wifi_detail = lv_label_create(s_wifi_cur_card);
  lv_label_set_text(s_lbl_wifi_detail, "Desconectado");
  lv_obj_set_style_text_color(s_lbl_wifi_detail, lv_color_hex(0x9CA3AF), 0);
  lv_obj_set_style_text_font(s_lbl_wifi_detail, FOCAL_FONT, 0);

  // Scrollable list for detected networks below status card (y=96, h=160 px)
  s_wifi_list = lv_obj_create(p0);
  lv_obj_set_size(s_wifi_list, 304, 160);
  lv_obj_set_pos(s_wifi_list, 12, 96);
  lv_obj_set_style_bg_color(
      s_wifi_list, s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xFFFFFF),
      0);
  lv_obj_set_style_border_width(s_wifi_list, 1, 0);
  lv_obj_set_style_border_color(
      s_wifi_list, s_is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xD1D5DB),
      0);
  lv_obj_set_style_radius(s_wifi_list, FOCAL_RADIUS_S, 0);
  lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_wifi_list, 4, 0);
  lv_obj_set_style_pad_row(s_wifi_list, 4, 0);

  // ==========================================
  // DETAIL PANEL 1: APARIENCIA (THEME & ACCENT)
  // ==========================================
  lv_obj_t *p1 = lv_obj_create(detail_area);
  lv_obj_set_size(p1, 328, 266);
  lv_obj_set_pos(p1, 0, 0);
  lv_obj_clear_flag(p1, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(p1, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p1, 0, 0);
  lv_obj_set_style_pad_all(p1, 0, 0);
  lv_obj_set_style_text_color(p1, lv_color_hex(s_is_dark ? 0xF9FAFB : 0x111827), 0);
  lv_obj_add_flag(p1, LV_OBJ_FLAG_HIDDEN);
  s_settings_panels[1] = p1;

  lv_obj_t *lt1 = lv_label_create(p1);
  lv_label_set_text(lt1, "Tema de Pantalla");
  lv_obj_set_pos(lt1, 12, 10);
  s_thm_lbl_title = lt1;

  lv_obj_t *btn_dark = focal_create_btn(p1, 146, 38, NULL, "Modo Oscuro",
                                        &focal_style_btn_secondary, 0);
  lv_obj_set_pos(btn_dark, 12, 34);
  lv_obj_set_ext_click_area(btn_dark, 6);
  lv_obj_add_event_cb(btn_dark, on_theme_toggle_cb, LV_EVENT_CLICKED,
                      (void *)true);
  s_thm_lbl_dark = lv_obj_get_child(btn_dark, 0);

  lv_obj_t *btn_light = focal_create_btn(p1, 146, 38, NULL, "Modo Claro",
                                         &focal_style_btn_secondary, 0);
  lv_obj_set_pos(btn_light, 170, 34);
  lv_obj_set_ext_click_area(btn_light, 6);
  lv_obj_add_event_cb(btn_light, on_theme_toggle_cb, LV_EVENT_CLICKED,
                      (void *)false);
  s_thm_lbl_light = lv_obj_get_child(btn_light, 0);

  lv_obj_t *lt2 = lv_label_create(p1);
  lv_label_set_text(lt2, "Color de Enfasis");
  lv_obj_set_pos(lt2, 12, 88);
  s_thm_lbl_accent = lt2;

  const char *color_names[5] = {"Azul", "Menta", "Ambar", "Lila", "Rojo"};
  for (int i = 0; i < 5; i++) {
    lv_obj_t *b = lv_btn_create(p1);
    lv_obj_set_size(b, 54, 38);
    lv_obj_set_pos(b, 13 + i * 62, 114);
    lv_obj_set_style_bg_color(b, lv_color_hex(s_accent_dark_hex[i]), 0);
    lv_obj_set_style_radius(b, FOCAL_RADIUS_M, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_ext_click_area(b, 4);
    lv_obj_add_event_cb(b, on_accent_select_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);

    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, color_names[i]);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
  }

  // ==========================================
  // DETAIL PANEL 2: LASER
  // ==========================================
  lv_obj_t *p2 = lv_obj_create(detail_area);
  lv_obj_set_size(p2, 328, 266);
  lv_obj_set_pos(p2, 0, 0);
  lv_obj_clear_flag(p2, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(p2, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p2, 0, 0);
  lv_obj_set_style_pad_all(p2, 0, 0);
  lv_obj_set_style_text_color(p2, lv_color_hex(s_is_dark ? 0xF9FAFB : 0x111827), 0);
  lv_obj_add_flag(p2, LV_OBJ_FLAG_HIDDEN);
  s_settings_panels[2] = p2;

  lv_obj_t *lt_lz = lv_label_create(p2);
  lv_label_set_text(lt_lz, "Parametros Modulo Laser");
  lv_obj_set_pos(lt_lz, 12, 10);
  s_lz_lbl_title = lt_lz;

  s_lbl_laser_pwr = lv_label_create(p2);
  lv_label_set_text(s_lbl_laser_pwr, "Potencia Maxima: 100%");
  lv_obj_set_style_text_color(s_lbl_laser_pwr, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_pos(s_lbl_laser_pwr, 12, 38);

  s_slider_laser_pwr = lv_slider_create(p2);
  lv_obj_set_size(s_slider_laser_pwr, 280, 18);
  lv_obj_set_pos(s_slider_laser_pwr, 24, 66);
  lv_slider_set_range(s_slider_laser_pwr, 10, 100);
  lv_slider_set_value(s_slider_laser_pwr, 100, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_slider_laser_pwr, FOCAL_COLOR_PASTEL_BLUE,
                            LV_PART_INDICATOR);
  lv_obj_add_event_cb(s_slider_laser_pwr, on_laser_slider_change_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *btn_test_lz =
      focal_create_btn(p2, 146, 42, &focal_icon_laser, "Pulso",
                       &focal_style_btn_amber, 0xFFFFFF);
  lv_obj_set_pos(btn_test_lz, 12, 120);
  lv_obj_set_ext_click_area(btn_test_lz, 6);
  lv_obj_add_event_cb(btn_test_lz, on_laser_pulse_btn_cb, LV_EVENT_CLICKED,
                      NULL);
  s_lz_btn_pulse_lbl = lv_obj_get_child(btn_test_lz, 1);

  lv_obj_t *btn_save_lz =
      focal_create_btn(p2, 146, 42, &focal_icon_check, "Guardar",
                       &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_set_pos(btn_save_lz, 170, 120);
  lv_obj_set_ext_click_area(btn_save_lz, 6);
  lv_obj_add_event_cb(btn_save_lz, on_save_laser_btn_cb, LV_EVENT_CLICKED,
                      NULL);

  // ==========================================
  // DETAIL PANEL 3: TARJETA SD
  // ==========================================
  lv_obj_t *p3 = lv_obj_create(detail_area);
  lv_obj_set_size(p3, 328, 266);
  lv_obj_set_pos(p3, 0, 0);
  lv_obj_clear_flag(p3, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(p3, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p3, 0, 0);
  lv_obj_set_style_pad_all(p3, 0, 0);
  lv_obj_set_style_text_color(p3, lv_color_hex(s_is_dark ? 0xF9FAFB : 0x111827), 0);
  lv_obj_add_flag(p3, LV_OBJ_FLAG_HIDDEN);
  s_settings_panels[3] = p3;

  lv_obj_t *lt_sd = lv_label_create(p3);
  lv_label_set_text(lt_sd, "Almacenamiento MicroSD");
  lv_obj_set_pos(lt_sd, 12, 10);
  s_sd_lbl_title = lt_sd;

  lv_obj_t *lbl_sd_use = lv_label_create(p3);
  lv_label_set_text(lbl_sd_use, "Uso: 1.9 GB / 16 GB (12%)");
  lv_obj_set_style_text_color(lbl_sd_use, FOCAL_COLOR_PASTEL_MINT, 0);
  lv_obj_set_pos(lbl_sd_use, 12, 36);

  lv_obj_t *bar_sd = lv_bar_create(p3);
  lv_obj_set_size(bar_sd, 304, 14);
  lv_obj_set_pos(bar_sd, 12, 62);
  lv_obj_set_style_bg_color(bar_sd, FOCAL_COLOR_PASTEL_MINT, LV_PART_INDICATOR);
  lv_bar_set_value(bar_sd, 12, LV_ANIM_OFF);

  // Group label and switch in row_ovr for perfect vertical alignment and closeness
  lv_obj_t *row_ovr = lv_obj_create(p3);
  lv_obj_set_size(row_ovr, 304, 32);
  lv_obj_set_pos(row_ovr, 12, 88);
  lv_obj_clear_flag(row_ovr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row_ovr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row_ovr, 0, 0);
  lv_obj_set_style_pad_all(row_ovr, 0, 0);
  lv_obj_set_flex_flow(row_ovr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row_ovr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row_ovr, 10, 0);

  s_lbl_ovr = lv_label_create(row_ovr);
  lv_label_set_text(s_lbl_ovr, "Sobrescribir duplicados:");
  lv_obj_set_style_text_font(s_lbl_ovr, FOCAL_FONT, 0);

  lv_obj_t *sw_ovr = lv_switch_create(row_ovr);
  lv_obj_set_size(sw_ovr, 38, 20);
  lv_obj_add_state(sw_ovr, LV_STATE_CHECKED);

  lv_obj_t *lbl_sd_stat = lv_label_create(p3);
  lv_label_set_text(lbl_sd_stat,
                    "Archivos en la SD: 4\nEspacio disponible: 14.1 GB");
  lv_obj_set_style_text_color(
      lbl_sd_stat, s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x4B5563),
      0);
  lv_obj_set_pos(lbl_sd_stat, 12, 126);

  lv_obj_t *btn_sd_fmt =
      focal_create_btn(p3, 304, 40, &focal_icon_trash, "Formatear SD",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_RED);
  lv_obj_set_pos(btn_sd_fmt, 12, 186);
  lv_obj_set_ext_click_area(btn_sd_fmt, 6);
  lv_obj_set_flex_align(btn_sd_fmt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  s_btn_sd_fmt_lbl = lv_obj_get_child(btn_sd_fmt, 1);
  if (s_btn_sd_fmt_lbl) {
    lv_obj_set_flex_grow(s_btn_sd_fmt_lbl, 0);
    lv_obj_set_style_text_align(s_btn_sd_fmt_lbl, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_add_event_cb(btn_sd_fmt, on_sd_fmt_btn_cb, LV_EVENT_CLICKED, NULL);

  // ==========================================
  // DETAIL PANEL 4: CINEMATICA
  // ==========================================
  lv_obj_t *p4 = lv_obj_create(detail_area);
  lv_obj_set_size(p4, 328, 266);
  lv_obj_set_pos(p4, 0, 0);
  lv_obj_clear_flag(p4, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(p4, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p4, 0, 0);
  lv_obj_set_style_pad_all(p4, 0, 0);
  lv_obj_set_style_text_color(p4, lv_color_hex(s_is_dark ? 0xF9FAFB : 0x111827), 0);
  lv_obj_add_flag(p4, LV_OBJ_FLAG_HIDDEN);
  s_settings_panels[4] = p4;

  lv_obj_t *lt_kin = lv_label_create(p4);
  lv_label_set_text(lt_kin, "Parametros de Movimiento");
  lv_obj_set_pos(lt_kin, 12, 10);
  s_kin_lbl_title = lt_kin;

  s_lbl_kin_feed = lv_label_create(p4);
  lv_label_set_text(s_lbl_kin_feed, "Velocidad Max: 6000 mm/min");
  lv_obj_set_style_text_color(s_lbl_kin_feed, FOCAL_COLOR_PASTEL_AMBER, 0);
  lv_obj_set_pos(s_lbl_kin_feed, 12, 34);

  s_slider_kin_feed = lv_slider_create(p4);
  lv_obj_set_size(s_slider_kin_feed, 280, 18);
  lv_obj_set_pos(s_slider_kin_feed, 24, 58);
  lv_slider_set_range(s_slider_kin_feed, 500, 8000);
  lv_slider_set_value(s_slider_kin_feed, 6000, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_slider_kin_feed, FOCAL_COLOR_PASTEL_AMBER,
                            LV_PART_INDICATOR);
  lv_obj_add_event_cb(s_slider_kin_feed, on_kin_feed_slider_change_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t *lbl_kin_bed_sec = lv_label_create(p4);
  lv_label_set_text(lbl_kin_bed_sec, "Dimensiones y Orientacion de Cama");
  lv_obj_set_pos(lbl_kin_bed_sec, 12, 88);
  s_kin_lbl_bed_sec = lbl_kin_bed_sec;

  s_lbl_kin_bed_info = lv_label_create(p4);
  char kin_bed_buf[64];
  snprintf(kin_bed_buf, sizeof(kin_bed_buf), "Cama: %d x %d mm  |  Rot: %d°",
           s_bed_w_mm, s_bed_h_mm, s_bed_rot);
  lv_label_set_text(s_lbl_kin_bed_info, kin_bed_buf);
  lv_obj_set_style_text_color(s_lbl_kin_bed_info, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_pos(s_lbl_kin_bed_info, 12, 110);

  s_btn_kin_rot =
      focal_create_btn(p4, 146, 38, &focal_icon_rot_cw, "Rotar",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_set_pos(s_btn_kin_rot, 12, 136);
  lv_obj_set_ext_click_area(s_btn_kin_rot, 6);
  lv_obj_add_event_cb(s_btn_kin_rot, on_kin_rot_click_cb, LV_EVENT_CLICKED,
                      NULL);
  s_btn_kin_rot_lbl = lv_obj_get_child(s_btn_kin_rot, 1);

  lv_obj_t *btn_kin_dim =
      focal_create_btn(p4, 146, 38, &focal_icon_bed, "Dimensiones",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_set_pos(btn_kin_dim, 170, 136);
  lv_obj_set_ext_click_area(btn_kin_dim, 6);
  lv_obj_add_event_cb(btn_kin_dim, on_kin_edit_dim_click_cb, LV_EVENT_CLICKED,
                      NULL);
  s_btn_kin_dim_lbl = lv_obj_get_child(btn_kin_dim, 1);

  lv_obj_t *btn_save_kin =
      focal_create_btn(p4, 304, 40, &focal_icon_check, "Guardar Parametros",
                       &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_set_pos(btn_save_kin, 12, 188);
  lv_obj_set_ext_click_area(btn_save_kin, 6);
  lv_obj_set_flex_align(btn_save_kin, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  s_btn_save_kin_lbl = lv_obj_get_child(btn_save_kin, 1);
  if (s_btn_save_kin_lbl) {
    lv_obj_set_flex_grow(s_btn_save_kin_lbl, 0);
    lv_obj_set_style_text_align(s_btn_save_kin_lbl, LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_add_event_cb(btn_save_kin, on_save_kin_btn_cb, LV_EVENT_CLICKED,
                      NULL);

  // ==========================================
  // DETAIL PANEL 5: SISTEMA & RESET
  // ==========================================
  lv_obj_t *p5 = lv_obj_create(detail_area);
  lv_obj_set_size(p5, 328, 266);
  lv_obj_set_pos(p5, 0, 0);
  lv_obj_clear_flag(p5, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(p5, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(p5, 0, 0);
  lv_obj_set_style_pad_all(p5, 0, 0);
  lv_obj_set_style_text_color(p5, lv_color_hex(s_is_dark ? 0xF9FAFB : 0x111827), 0);
  lv_obj_add_flag(p5, LV_OBJ_FLAG_HIDDEN);
  s_settings_panels[5] = p5;

  lv_obj_t *lt_sys = lv_label_create(p5);
  lv_label_set_text(lt_sys, "Hardware y Firmware");
  lv_obj_set_pos(lt_sys, 12, 10);
  s_sys_lbl_title = lt_sys;

  lv_obj_t *lbl_sys_val = lv_label_create(p5);
  lv_label_set_text(
      lbl_sys_val,
      "SoC: ESP32-S3 Dual-Core 240MHz\nRAM: 8MB PSRAM | Flash: 16MB\nFirmware: "
      "Focal CNC v1.0\nConexion: Red Wi-Fi Local (2.4 GHz)");
  lv_obj_set_pos(lbl_sys_val, 12, 32);

  lv_obj_t *btn_legal =
      focal_create_btn(p5, 146, 40, &focal_icon_info, "Aviso Legal",
                       &focal_style_btn_secondary, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_set_pos(btn_legal, 12, 120);
  lv_obj_set_ext_click_area(btn_legal, 6);
  lv_obj_add_event_cb(btn_legal, on_legal_btn_cb, LV_EVENT_CLICKED, NULL);
  s_btn_legal_lbl = lv_obj_get_child(btn_legal, 1);

  lv_obj_t *btn_rst =
      focal_create_btn(p5, 146, 40, &focal_icon_trash, "Reset Fabrica",
                       &focal_style_btn_danger, 0xFFFFFF);
  lv_obj_set_pos(btn_rst, 170, 120);
  lv_obj_set_ext_click_area(btn_rst, 6);
  lv_obj_add_event_cb(btn_rst, on_reset_btn_cb, LV_EVENT_CLICKED, NULL);
  s_btn_rst_lbl = lv_obj_get_child(btn_rst, 1);

  lv_obj_t *lbl_sys_copy = lv_label_create(p5);
  lv_label_set_text(lbl_sys_copy, "(c) 2026 Jhoel Ospino • Focal CNC v1.0");
  lv_obj_set_style_text_color(
      lbl_sys_copy, s_is_dark ? lv_color_hex(0x6B7280) : lv_color_hex(0x9CA3AF), 0);
  lv_obj_align(lbl_sys_copy, LV_ALIGN_BOTTOM_MID, 0, -6);

  // ==========================================
  // CONFIRMATION MODAL OVERLAY (APPLE HIG SCRIM)
  // ==========================================
  s_settings_confirm_modal = lv_obj_create(scr);
  lv_obj_set_size(s_settings_confirm_modal, 480, 320);
  lv_obj_set_pos(s_settings_confirm_modal, 0, 0);
  lv_obj_set_style_bg_color(s_settings_confirm_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_settings_confirm_modal, LV_OPA_70, 0);
  lv_obj_set_style_pad_all(s_settings_confirm_modal, 0, 0);
  lv_obj_set_style_border_width(s_settings_confirm_modal, 0, 0);
  lv_obj_clear_flag(s_settings_confirm_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_settings_confirm_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *c_diag = lv_obj_create(s_settings_confirm_modal);
  lv_obj_set_size(c_diag, 340, 170);
  lv_obj_center(c_diag);
  lv_obj_add_style(c_diag, &focal_style_card, 0);
  lv_obj_clear_flag(c_diag, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(c_diag, 12, 0);

  lv_obj_t *box_c_head = lv_obj_create(c_diag);
  lv_obj_set_size(box_c_head, 316, 28);
  lv_obj_align(box_c_head, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(box_c_head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box_c_head, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box_c_head, 0, 0);
  lv_obj_set_style_pad_all(box_c_head, 0, 0);
  lv_obj_set_flex_flow(box_c_head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(box_c_head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(box_c_head, 8, 0);

  s_confirm_icon = focal_create_icon(box_c_head, &focal_icon_check, FOCAL_HEX_PASTEL_MINT);
  s_confirm_title_lbl = lv_label_create(box_c_head);
  lv_label_set_text(s_confirm_title_lbl, "Confirmacion");

  s_confirm_msg_lbl = lv_label_create(c_diag);
  lv_obj_set_width(s_confirm_msg_lbl, 310);
  lv_label_set_long_mode(s_confirm_msg_lbl, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_confirm_msg_lbl, "Deseas aplicar estos cambios?");
  lv_obj_set_style_text_align(s_confirm_msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_confirm_msg_lbl, LV_ALIGN_CENTER, 0, -6);

  lv_obj_t *btn_c_canc = focal_create_btn(c_diag, 146, 38, NULL, "Cancelar",
                                          &focal_style_btn_secondary, 0);
  lv_obj_align(btn_c_canc, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_ext_click_area(btn_c_canc, 6);
  lv_obj_add_event_cb(btn_c_canc, on_confirm_modal_cancel_cb, LV_EVENT_CLICKED,
                      NULL);

  s_confirm_btn_ok = focal_create_btn(c_diag, 146, 38, &focal_icon_check, "Aceptar",
                                      &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_align(s_confirm_btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_ext_click_area(s_confirm_btn_ok, 6);
  lv_obj_add_event_cb(s_confirm_btn_ok, on_confirm_modal_ok_cb, LV_EVENT_CLICKED, NULL);

  // ==========================================
  // LEGAL NOTICE MODAL (APPLE HIG SCROLLABLE NOTICE)
  // ==========================================
  s_legal_modal = lv_obj_create(scr);
  lv_obj_set_size(s_legal_modal, 480, 320);
  lv_obj_set_pos(s_legal_modal, 0, 0);
  lv_obj_set_style_bg_color(s_legal_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_legal_modal, LV_OPA_70, 0);
  lv_obj_set_style_pad_all(s_legal_modal, 0, 0);
  lv_obj_set_style_border_width(s_legal_modal, 0, 0);
  lv_obj_clear_flag(s_legal_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_legal_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *l_diag = lv_obj_create(s_legal_modal);
  lv_obj_set_size(l_diag, 420, 260);
  lv_obj_center(l_diag);
  lv_obj_add_style(l_diag, &focal_style_card, 0);
  lv_obj_clear_flag(l_diag, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(l_diag, 12, 0);

  lv_obj_t *box_l_head = lv_obj_create(l_diag);
  lv_obj_set_size(box_l_head, 396, 28);
  lv_obj_align(box_l_head, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(box_l_head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box_l_head, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box_l_head, 0, 0);
  lv_obj_set_style_pad_all(box_l_head, 0, 0);
  lv_obj_set_flex_flow(box_l_head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(box_l_head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(box_l_head, 8, 0);

  focal_create_icon(box_l_head, &focal_icon_info, FOCAL_HEX_PASTEL_BLUE);
  lv_obj_t *lbl_l_title = lv_label_create(box_l_head);
  lv_label_set_text(lbl_l_title, "Aviso Legal y Derechos de Autor");
  lv_obj_set_style_text_color(lbl_l_title, FOCAL_COLOR_PASTEL_BLUE, 0);

  lv_obj_t *l_scroll = lv_obj_create(l_diag);
  lv_obj_set_size(l_scroll, 396, 144);
  lv_obj_align(l_scroll, LV_ALIGN_CENTER, 0, -4);
  lv_obj_set_style_bg_color(
      l_scroll, s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(l_scroll, 1, 0);
  lv_obj_set_style_border_color(
      l_scroll, s_is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xD1D5DB), 0);
  lv_obj_set_style_radius(l_scroll, FOCAL_RADIUS_S, 0);
  lv_obj_set_style_pad_all(l_scroll, 8, 0);

  lv_obj_t *lbl_notice = lv_label_create(l_scroll);
  lv_obj_set_width(lbl_notice, 372);
  lv_label_set_long_mode(lbl_notice, LV_LABEL_LONG_WRAP);
  lv_label_set_text(
      lbl_notice,
      "(c) 2026 Jhoel Ospino. All rights reserved.\n\n"
      "The Focal CNC firmware, control software, and hardware designs of the "
      "FOCAL V1 machine are proprietary and protected by intellectual property laws. "
      "Unauthorized copying, distribution, modification, or reverse engineering of any "
      "part of this system is strictly prohibited without prior written consent from "
      "the copyright owner.");
  lv_obj_set_style_text_color(
      lbl_notice, s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827), 0);

  lv_obj_t *btn_l_ok = focal_create_btn(l_diag, 140, 36, &focal_icon_check, "Entendido",
                                        &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_align(btn_l_ok, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_ext_click_area(btn_l_ok, 6);
  lv_obj_add_event_cb(btn_l_ok, on_legal_modal_close_cb, LV_EVENT_CLICKED, NULL);

  // ==========================================
  // FLOATING KEYBOARD MODAL FOR WI-FI INPUT (APPLE HIG MODAL SHEET)
  // ==========================================
  s_wifi_modal = lv_obj_create(scr);
  lv_obj_set_size(s_wifi_modal, 480, 320);
  lv_obj_set_pos(s_wifi_modal, 0, 0);
  lv_obj_set_style_bg_color(
      s_wifi_modal, s_is_dark ? lv_color_hex(0x0F1115) : lv_color_hex(0xF5F6F8),
      0);
  lv_obj_set_style_bg_opa(s_wifi_modal, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_wifi_modal, 0, 0);
  lv_obj_set_style_border_width(s_wifi_modal, 0, 0);
  lv_obj_clear_flag(s_wifi_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);

  // Top Nav Row: Cancel Button (Left)
  lv_obj_t *btn_modal_canc = focal_create_btn(
      s_wifi_modal, 96, 36, NULL, "Cancelar", &focal_style_btn_secondary, 0);
  lv_obj_set_pos(btn_modal_canc, 12, 10);
  lv_obj_set_ext_click_area(btn_modal_canc, 6);
  lv_obj_add_event_cb(btn_modal_canc, on_modal_cancel_cb, LV_EVENT_CLICKED,
                      NULL);

  // Top Nav Row: Network Title (Centered)
  s_modal_ssid_lbl = lv_label_create(s_wifi_modal);
  lv_label_set_text(s_modal_ssid_lbl, "Red: Wi-Fi");
  lv_obj_set_style_text_color(s_modal_ssid_lbl, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_align(s_modal_ssid_lbl, LV_ALIGN_TOP_MID, 0, 18);

  // Top Nav Row: Connect Button (Right)
  lv_obj_t *btn_modal_conn =
      focal_create_btn(s_wifi_modal, 106, 36, &focal_icon_check, "Conectar",
                       &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_set_pos(btn_modal_conn, 362, 10);
  lv_obj_set_ext_click_area(btn_modal_conn, 6);
  lv_obj_add_event_cb(btn_modal_conn, on_modal_connect_cb, LV_EVENT_CLICKED,
                      NULL);

  // Middle Row: Full-Width Password Input Field
  s_modal_pass_ta = lv_textarea_create(s_wifi_modal);
  lv_obj_set_size(s_modal_pass_ta, 456, 44);
  lv_obj_set_pos(s_modal_pass_ta, 12, 54);
  lv_textarea_set_placeholder_text(s_modal_pass_ta, "Ingresa clave Wi-Fi...");
  lv_textarea_set_password_mode(s_modal_pass_ta, true);
  lv_textarea_set_one_line(s_modal_pass_ta, true);
  lv_obj_set_style_radius(s_modal_pass_ta, FOCAL_RADIUS_M, 0);

  // Bottom Keyboard (Height 212px, perfectly docked at bottom)
  lv_obj_t *kb = lv_keyboard_create(s_wifi_modal);
  lv_obj_set_size(kb, 480, 212);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, s_modal_pass_ta);
}

// =============================================================================
// SCREEN 7: EDITOR DE DIMENSIONES Y ORIENTACION DE CAMA
// =============================================================================
static void update_bed_dimensions_preview(void) {
  if (s_bed_dim_lbl_x) {
    char buf[32];
    snprintf(buf, sizeof(buf), "X: %d mm", s_edit_bed_w);
    lv_label_set_text(s_bed_dim_lbl_x, buf);
  }
  if (s_bed_dim_lbl_y) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Y: %d mm", s_edit_bed_h);
    lv_label_set_text(s_bed_dim_lbl_y, buf);
  }

  if (s_bed_dim_btn_x && s_bed_dim_btn_y) {
    uint32_t inactive_border = s_is_dark ? 0x2D333F : 0xD1D5DB;
    uint32_t inactive_bg = s_is_dark ? 0x1A1D24 : 0xF3F4F6;
    uint32_t active_bg = s_is_dark ? 0x222732 : 0xE5E7EB;

    if (s_edit_active_field == 0) {
      lv_obj_set_style_border_color(s_bed_dim_btn_x, focal_theme_get_accent_color(), 0);
      lv_obj_set_style_border_width(s_bed_dim_btn_x, 2, 0);
      lv_obj_set_style_bg_color(s_bed_dim_btn_x, lv_color_hex(active_bg), 0);

      lv_obj_set_style_border_color(s_bed_dim_btn_y, lv_color_hex(inactive_border), 0);
      lv_obj_set_style_border_width(s_bed_dim_btn_y, 1, 0);
      lv_obj_set_style_bg_color(s_bed_dim_btn_y, lv_color_hex(inactive_bg), 0);
    } else {
      lv_obj_set_style_border_color(s_bed_dim_btn_y, focal_theme_get_accent_color(), 0);
      lv_obj_set_style_border_width(s_bed_dim_btn_y, 2, 0);
      lv_obj_set_style_bg_color(s_bed_dim_btn_y, lv_color_hex(active_bg), 0);

      lv_obj_set_style_border_color(s_bed_dim_btn_x, lv_color_hex(inactive_border), 0);
      lv_obj_set_style_border_width(s_bed_dim_btn_x, 1, 0);
      lv_obj_set_style_bg_color(s_bed_dim_btn_x, lv_color_hex(inactive_bg), 0);
    }
  }

  if (s_bed_dim_sim_box) {
    int eff_w = (s_bed_rot == 90 || s_bed_rot == 270) ? s_edit_bed_h : s_edit_bed_w;
    int eff_h = (s_bed_rot == 90 || s_bed_rot == 270) ? s_edit_bed_w : s_edit_bed_h;
    if (eff_w < 50) eff_w = 50;
    if (eff_h < 50) eff_h = 50;

    float scale = fminf(140.0f / (float)eff_w, 160.0f / (float)eff_h);
    int bw = (int)(eff_w * scale);
    int bh = (int)(eff_h * scale);
    if (bw < 30) bw = 30;
    if (bh < 30) bh = 30;

    lv_obj_set_size(s_bed_dim_sim_box, bw, bh);
    lv_obj_align(s_bed_dim_sim_box, LV_ALIGN_CENTER, 0, -10);
  }

  if (s_bed_dim_lbl_info) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d x %d mm  (%d°)", s_edit_bed_w, s_edit_bed_h, s_bed_rot);
    lv_label_set_text(s_bed_dim_lbl_info, buf);
  }
}

static void on_bed_dim_field_sel_cb(lv_event_t *e) {
  int field = (int)(uintptr_t)lv_event_get_user_data(e);
  s_edit_active_field = field;
  if (field == 0) {
    snprintf(s_keypad_buf, sizeof(s_keypad_buf), "%d", s_edit_bed_w);
  } else {
    snprintf(s_keypad_buf, sizeof(s_keypad_buf), "%d", s_edit_bed_h);
  }
  update_bed_dimensions_preview();
}

static void on_bed_dim_key_cb(lv_event_t *e) {
  const char *k = (const char *)lv_event_get_user_data(e);
  if (!k) return;

  if (strcmp(k, "DEL") == 0 || strcmp(k, "BKSP") == 0) {
    size_t len = strlen(s_keypad_buf);
    if (len > 1) {
      s_keypad_buf[len - 1] = '\0';
    } else {
      strcpy(s_keypad_buf, "0");
    }
  } else if (strcmp(k, "C") == 0) {
    strcpy(s_keypad_buf, "0");
  } else if (strcmp(k, ".") == 0) {
    if (strchr(s_keypad_buf, '.') == NULL && strlen(s_keypad_buf) < 5) {
      strncat(s_keypad_buf, ".", sizeof(s_keypad_buf) - strlen(s_keypad_buf) - 1);
    }
  } else if (k[0] >= '0' && k[0] <= '9') {
    if (strlen(s_keypad_buf) < 4) {
      if (strcmp(s_keypad_buf, "0") == 0) {
        snprintf(s_keypad_buf, sizeof(s_keypad_buf), "%s", k);
      } else {
        strncat(s_keypad_buf, k, sizeof(s_keypad_buf) - strlen(s_keypad_buf) - 1);
      }
    }
  }

  int val = atoi(s_keypad_buf);
  if (s_edit_active_field == 0) {
    s_edit_bed_w = val;
  } else {
    s_edit_bed_h = val;
  }
  update_bed_dimensions_preview();
}

static void on_bed_dim_cancel_cb(lv_event_t *e) {
  s_edit_bed_w = s_bed_w_mm;
  s_edit_bed_h = s_bed_h_mm;
  ui_switch_screen(UI_SCREEN_SETTINGS);
  ui_settings_select_tab(4);
}

static void do_bed_dim_confirm_apply(void) {
  if (s_edit_bed_w < 50) s_edit_bed_w = 50;
  if (s_edit_bed_w > 1200) s_edit_bed_w = 1200;
  if (s_edit_bed_h < 50) s_edit_bed_h = 50;
  if (s_edit_bed_h > 1200) s_edit_bed_h = 1200;

  ui_set_bed_dimensions(s_edit_bed_w, s_edit_bed_h, s_bed_rot);
  if (s_bed_dim_confirm_modal) {
    lv_obj_add_flag(s_bed_dim_confirm_modal, LV_OBJ_FLAG_HIDDEN);
  }
  ui_switch_screen(UI_SCREEN_SETTINGS);
  ui_settings_select_tab(4);
}

static void on_bed_dim_confirm_cancel_cb(lv_event_t *e) {
  if (s_bed_dim_confirm_modal) {
    lv_obj_add_flag(s_bed_dim_confirm_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

static void on_bed_dim_confirm_ok_cb(lv_event_t *e) {
  do_bed_dim_confirm_apply();
}

static void on_bed_dim_apply_cb(lv_event_t *e) {
  if (s_bed_dim_confirm_modal) {
    lv_obj_t *diag = lv_obj_get_child(s_bed_dim_confirm_modal, 0);
    if (diag) {
      lv_obj_t *lbl_msg = lv_obj_get_child(diag, 1);
      if (lbl_msg) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Guardar dimensiones de corte?\n%d x %d mm",
                 s_edit_bed_w, s_edit_bed_h);
        lv_label_set_text(lbl_msg, buf);
      }
    }
    lv_obj_clear_flag(s_bed_dim_confirm_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_bed_dim_confirm_modal);
  }
}

static void create_bed_dimensions_screen(void) {
  lv_obj_t *scr = lv_obj_create(NULL);
  s_screens[UI_SCREEN_BED_DIMENSIONS] = scr;
  lv_obj_add_style(scr, &focal_style_screen, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_screen_header(UI_SCREEN_BED_DIMENSIONS, "Dimensiones de Cama", true);

  // Left Container: Real-time scale preview of the cutting bed
  lv_obj_t *card_prev = lv_obj_create(scr);
  lv_obj_set_size(card_prev, 196, 264);
  lv_obj_set_pos(card_prev, 10, 46);
  lv_obj_add_style(card_prev, &focal_style_card, 0);
  lv_obj_clear_flag(card_prev, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(card_prev, 6, 0);
  s_bed_dim_card_prev = card_prev;

  lv_obj_t *lbl_prev_title = lv_label_create(card_prev);
  lv_label_set_text(lbl_prev_title, "Vista Previa de Cama");
  lv_obj_set_style_text_color(
      lbl_prev_title, s_is_dark ? lv_color_hex(0x9CA3AF) : lv_color_hex(0x4B5563),
      0);
  lv_obj_align(lbl_prev_title, LV_ALIGN_TOP_MID, 0, 2);
  s_bed_dim_lbl_prev_title = lbl_prev_title;

  // Bed box object
  s_bed_dim_sim_box = lv_obj_create(card_prev);
  lv_obj_set_size(s_bed_dim_sim_box, 120, 150);
  lv_obj_align(s_bed_dim_sim_box, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(
      s_bed_dim_sim_box,
      s_is_dark ? lv_color_hex(0x14171E) : lv_color_hex(0xE5E7EB), 0);
  lv_obj_set_style_border_color(s_bed_dim_sim_box, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_set_style_border_width(s_bed_dim_sim_box, 1, 0);
  lv_obj_set_style_radius(s_bed_dim_sim_box, FOCAL_RADIUS_S, 0);
  lv_obj_clear_flag(s_bed_dim_sim_box, LV_OBJ_FLAG_SCROLLABLE);

  // Origin indicator (0,0) in bottom-left
  lv_obj_t *orig_dot = lv_obj_create(s_bed_dim_sim_box);
  lv_obj_set_size(orig_dot, 8, 8);
  lv_obj_set_style_radius(orig_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(orig_dot, FOCAL_COLOR_PASTEL_RED, 0);
  lv_obj_set_style_border_width(orig_dot, 0, 0);
  lv_obj_align(orig_dot, LV_ALIGN_BOTTOM_LEFT, 2, -2);

  // Readout at bottom of preview
  s_bed_dim_lbl_info = lv_label_create(card_prev);
  lv_label_set_text(s_bed_dim_lbl_info, "160 x 200 mm  (0°)");
  lv_obj_set_style_text_color(s_bed_dim_lbl_info, FOCAL_COLOR_PASTEL_BLUE, 0);
  lv_obj_align(s_bed_dim_lbl_info, LV_ALIGN_BOTTOM_MID, 0, -4);

  // Right Container: Dimensions input fields and keypad
  lv_obj_t *card_ctrl = lv_obj_create(scr);
  lv_obj_set_size(card_ctrl, 254, 264);
  lv_obj_set_pos(card_ctrl, 216, 46);
  lv_obj_add_style(card_ctrl, &focal_style_card, 0);
  lv_obj_clear_flag(card_ctrl, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(card_ctrl, 0, 0);

  // Top row: Field X and Field Y selection buttons (Width 113 each, gap 8)
  s_bed_dim_btn_x = focal_create_btn(card_ctrl, 113, 36, NULL, "X: 160 mm",
                                     &focal_style_btn_secondary, 0);
  lv_obj_set_pos(s_bed_dim_btn_x, 10, 8);
  s_bed_dim_lbl_x = lv_obj_get_child(s_bed_dim_btn_x, 0);
  lv_obj_add_event_cb(s_bed_dim_btn_x, on_bed_dim_field_sel_cb, LV_EVENT_CLICKED,
                      (void *)0);

  s_bed_dim_btn_y = focal_create_btn(card_ctrl, 113, 36, NULL, "Y: 200 mm",
                                     &focal_style_btn_secondary, 0);
  lv_obj_set_pos(s_bed_dim_btn_y, 131, 8);
  s_bed_dim_lbl_y = lv_obj_get_child(s_bed_dim_btn_y, 0);
  lv_obj_add_event_cb(s_bed_dim_btn_y, on_bed_dim_field_sel_cb, LV_EVENT_CLICKED,
                      (void *)1);

  // Keypad: 3 rows x 4 cols
  const char *keys[3][4] = {
      {"1", "2", "3", "DEL"},
      {"4", "5", "6", "."},
      {"7", "8", "9", "0"}
  };

  // Row 0, 1, 2: 4 buttons each of width 54, height 36, gap 6, row step 42
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 4; c++) {
      lv_obj_t *bk;
      if (r == 0 && c == 3) {
        // Backspace key with vector delete icon
        bk = focal_create_btn(card_ctrl, 54, 36, &focal_icon_backspace, NULL,
                              &focal_style_btn_secondary, FOCAL_HEX_PASTEL_AMBER);
      } else {
        bk = focal_create_btn(card_ctrl, 54, 36, NULL, keys[r][c],
                              &focal_style_btn_secondary, 0);
      }
      lv_obj_set_pos(bk, 10 + c * 60, 52 + r * 42);
      lv_obj_add_event_cb(bk, on_bed_dim_key_cb, LV_EVENT_CLICKED,
                          (void *)keys[r][c]);
    }
  }

  // Row 3: Cancel (width 113) and Apply (width 113)
  lv_obj_t *btn_k_canc = focal_create_btn(card_ctrl, 113, 38, NULL, "Cancelar",
                                          &focal_style_btn_secondary, 0);
  lv_obj_set_pos(btn_k_canc, 10, 184);
  lv_obj_add_event_cb(btn_k_canc, on_bed_dim_cancel_cb, LV_EVENT_CLICKED, NULL);
  s_bdim_btn_canc_lbl = lv_obj_get_child(btn_k_canc, 0);

  lv_obj_t *btn_k_ok = focal_create_btn(card_ctrl, 113, 38, &focal_icon_check,
                                        "Guardar", &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_set_pos(btn_k_ok, 131, 184);
  lv_obj_add_event_cb(btn_k_ok, on_bed_dim_apply_cb, LV_EVENT_CLICKED, NULL);
  s_bdim_btn_ok_lbl = lv_obj_get_child(btn_k_ok, 1);

  // Confirmation Modal on Bed Dimensions Screen
  s_bed_dim_confirm_modal = lv_obj_create(scr);
  lv_obj_set_size(s_bed_dim_confirm_modal, 480, 320);
  lv_obj_set_pos(s_bed_dim_confirm_modal, 0, 0);
  lv_obj_set_style_bg_color(s_bed_dim_confirm_modal, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(s_bed_dim_confirm_modal, LV_OPA_70, 0);
  lv_obj_set_style_pad_all(s_bed_dim_confirm_modal, 0, 0);
  lv_obj_set_style_border_width(s_bed_dim_confirm_modal, 0, 0);
  lv_obj_clear_flag(s_bed_dim_confirm_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_bed_dim_confirm_modal, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *b_diag = lv_obj_create(s_bed_dim_confirm_modal);
  lv_obj_set_size(b_diag, 340, 170);
  lv_obj_center(b_diag);
  lv_obj_add_style(b_diag, &focal_style_card, 0);
  lv_obj_clear_flag(b_diag, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(b_diag, 12, 0);

  lv_obj_t *box_b_head = lv_obj_create(b_diag);
  lv_obj_set_size(box_b_head, 316, 28);
  lv_obj_align(box_b_head, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(box_b_head, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box_b_head, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box_b_head, 0, 0);
  lv_obj_set_style_pad_all(box_b_head, 0, 0);
  lv_obj_set_flex_flow(box_b_head, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(box_b_head, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(box_b_head, 8, 0);

  focal_create_icon(box_b_head, &focal_icon_check, FOCAL_HEX_PASTEL_MINT);
  lv_obj_t *lbl_b_title = lv_label_create(box_b_head);
  lv_label_set_text(lbl_b_title, "Guardar Dimensiones");
  lv_obj_set_style_text_color(lbl_b_title, FOCAL_COLOR_PASTEL_MINT, 0);
  s_bdim_modal_title_lbl = lbl_b_title;

  lv_obj_t *lbl_b_msg = lv_label_create(b_diag);
  lv_obj_set_width(lbl_b_msg, 310);
  lv_label_set_long_mode(lbl_b_msg, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl_b_msg, "Guardar nuevas dimensiones de cama?");
  lv_obj_set_style_text_align(lbl_b_msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl_b_msg, LV_ALIGN_CENTER, 0, -6);

  lv_obj_t *btn_b_canc = focal_create_btn(b_diag, 146, 38, NULL, "Cancelar",
                                          &focal_style_btn_secondary, 0);
  lv_obj_align(btn_b_canc, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_ext_click_area(btn_b_canc, 6);
  lv_obj_add_event_cb(btn_b_canc, on_bed_dim_confirm_cancel_cb, LV_EVENT_CLICKED,
                      NULL);
  s_bdim_modal_canc_lbl = lv_obj_get_child(btn_b_canc, 0);

  lv_obj_t *btn_b_ok = focal_create_btn(b_diag, 146, 38, &focal_icon_check,
                                        "Guardar", &focal_style_btn_mint, 0xFFFFFF);
  lv_obj_align(btn_b_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_ext_click_area(btn_b_ok, 6);
  lv_obj_add_event_cb(btn_b_ok, on_bed_dim_confirm_ok_cb, LV_EVENT_CLICKED,
                      NULL);
  s_bdim_modal_ok_lbl = lv_obj_get_child(btn_b_ok, 1);

  update_bed_dimensions_preview();
}

// =============================================================================
// GLOBAL INTERFACE INITIALIZATION & DISPATCH
// =============================================================================
static void ui_wifi_poll_timer_cb(lv_timer_t *timer);

void ui_init(void) {
  ESP_LOGI(
      TAG,
      "Inicializando Focal CNC UI (Launcher 3x2 con Iconos Vectoriales)...");

  focal_theme_init();
  settings_manager_init();

  machine_settings_t cfg;
  settings_manager_get(&cfg);
  if (cfg.travel_max_x > 0) s_bed_w_mm = (int)cfg.travel_max_x;
  if (cfg.travel_max_y > 0) s_bed_h_mm = (int)cfg.travel_max_y;
  s_bed_rot = cfg.bed_rotation;

  s_num_favorites = cfg.num_favorites;
  if (s_num_favorites > 2) s_num_favorites = 2;
  strncpy(s_favorite_files[0], cfg.favorite_files[0], sizeof(s_favorite_files[0]) - 1);
  s_favorite_files[0][sizeof(s_favorite_files[0]) - 1] = '\0';
  strncpy(s_favorite_files[1], cfg.favorite_files[1], sizeof(s_favorite_files[1]) - 1);
  s_favorite_files[1][sizeof(s_favorite_files[1]) - 1] = '\0';

  // Crear las 8 pantallas
  create_launcher_screen();
  create_jogging_screen();
  create_sd_screen();
  create_cut_monitor_screen();
  create_calibration_screen();
  create_shapes_screen();
  update_shapes_favorite_buttons();
  create_settings_screen();
  create_bed_dimensions_screen();

  // Timer periódico para refresco inmediato de estado Wi-Fi e IP (cada 300 ms)
  lv_timer_create(ui_wifi_poll_timer_cb, 300, NULL);

  // Iniciar directamente en el Launcher 3x2
  ui_update_wifi_display();
  s_current_screen = UI_SCREEN_LAUNCHER;
  lv_scr_load(s_screens[UI_SCREEN_LAUNCHER]);

  ESP_LOGI(TAG, "UI Focal CNC lista con transiciones instantaneas.");
}

static void ui_wifi_poll_timer_cb(lv_timer_t *timer) {
  (void)timer;
  static bool last_conn = false;
  static bool last_connecting = false;
  static char last_ip[32] = "";
  static char last_ssid[33] = "";

  bool conn = wifi_manager_is_sta_connected();
  bool connecting = wifi_manager_is_sta_connecting();
  const char *cur_ip = wifi_manager_get_sta_ip();
  const char *cur_ssid = wifi_manager_get_sta_ssid();

  if (conn != last_conn || connecting != last_connecting ||
      strcmp(cur_ip ? cur_ip : "", last_ip) != 0 ||
      strcmp(cur_ssid ? cur_ssid : "", last_ssid) != 0) {
    last_conn = conn;
    last_connecting = connecting;
    if (cur_ip) strncpy(last_ip, cur_ip, sizeof(last_ip) - 1);
    if (cur_ssid) strncpy(last_ssid, cur_ssid, sizeof(last_ssid) - 1);
    ui_update_wifi_display();
  }
}

void ui_switch_screen(ui_screen_id_t screen_id) {
  if (screen_id >= UI_SCREEN_MAX || !s_screens[screen_id])
    return;

  // Safety hook: If leaving screen and not running a cut job, force laser off
  if (s_current_screen != screen_id && !s_is_cutting) {
    laser_set_power(0);
    laser_set_enable(false);
    s_calib_pointer_on = false;
    if (s_calib_btn_ptr) {
      lv_obj_set_style_bg_color(
          s_calib_btn_ptr,
          s_is_dark ? lv_color_hex(0x1A1D24) : lv_color_hex(0xFFFFFF), 0);
    }
  }

  // Reset to clean default state when navigating between screens
  if (s_current_screen != screen_id) {
    if (screen_id == UI_SCREEN_SETTINGS) {
      if (s_current_screen != UI_SCREEN_BED_DIMENSIONS) {
        ui_settings_select_tab(0);
      }
    } else if (screen_id == UI_SCREEN_CALIBRATION) {
      s_calib_step = 0;
      s_calib_done_mask = 0;
      calib_update_view();
    }
    // Ocultar cualquier modal remanente
    if (s_settings_confirm_modal) lv_obj_add_flag(s_settings_confirm_modal, LV_OBJ_FLAG_HIDDEN);
    if (s_wifi_modal) lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
    if (s_sd_options_modal) lv_obj_add_flag(s_sd_options_modal, LV_OBJ_FLAG_HIDDEN);
    if (s_sd_del_modal) lv_obj_add_flag(s_sd_del_modal, LV_OBJ_FLAG_HIDDEN);
    if (s_cut_modal) lv_obj_add_flag(s_cut_modal, LV_OBJ_FLAG_HIDDEN);
    if (s_legal_modal) lv_obj_add_flag(s_legal_modal, LV_OBJ_FLAG_HIDDEN);
  }

  s_current_screen = screen_id;
  lv_scr_load(s_screens[screen_id]);
  if (screen_id == UI_SCREEN_JOGGING && s_jog_lock_modal) {
    if (s_is_cutting) {
      lv_obj_clear_flag(s_jog_lock_modal, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_jog_lock_modal, LV_OBJ_FLAG_HIDDEN);
    }
  }
  ui_update_wifi_display();
}

ui_screen_id_t ui_get_current_screen(void) { return s_current_screen; }

void ui_set_cutting_state(bool is_cutting) {
  s_is_cutting = is_cutting;
  update_mon_pause_btn(is_cutting);
  if (s_cut_liquid_wave) {
    liquid_wave_progress_set_animating(s_cut_liquid_wave, is_cutting);
  }
  if (s_current_screen == UI_SCREEN_JOGGING && s_jog_lock_modal) {
    if (s_is_cutting) {
      lv_obj_clear_flag(s_jog_lock_modal, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_jog_lock_modal, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

bool ui_is_cutting(void) {
  return s_is_cutting;
}

void ui_show_stop_modal(bool show) {
  if (!s_stop_modal) return;
  if (show) {
    lv_obj_clear_flag(s_stop_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_stop_modal);
  } else {
    lv_obj_add_flag(s_stop_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_set_demo_mode(bool enable) { (void)enable; }

void ui_update_wifi_display(void) {
  bool is_conn = wifi_manager_is_sta_connected();
  bool is_connecting = wifi_manager_is_sta_connecting();
  const char *ssid = wifi_manager_get_sta_ssid();
  const char *ip = wifi_manager_get_sta_ip();
  int rssi = wifi_manager_get_sta_rssi();

  const char *hdr_text = "Sin Wi-Fi";
  const void *ic_src = &focal_icon_wifi_dot;
  uint32_t ic_color = 0x8E8E93;

  if (is_conn && ssid && strlen(ssid) > 0) {
    hdr_text = ssid;
    ic_src = focal_get_wifi_icon_by_rssi(rssi);
    ic_color = FOCAL_HEX_PASTEL_MINT;
  } else if (is_connecting) {
    hdr_text = "Conectando...";
    ic_src = &focal_icon_wifi_dot;
    ic_color = FOCAL_HEX_PASTEL_AMBER;
  } else {
    hdr_text = "Sin Wi-Fi";
    ic_src = &focal_icon_wifi_dot;
    ic_color = 0x8E8E93;
  }

  // 1. Refrescar el cuadro de información en la esquina superior derecha en todas las pantallas
  for (int i = 0; i < UI_SCREEN_MAX; i++) {
    if (s_hdr_pills[i]) {
      lv_label_set_text(s_hdr_pills[i], hdr_text);
      lv_obj_set_style_text_color(s_hdr_pills[i],
          is_connecting ? lv_color_hex(FOCAL_HEX_PASTEL_AMBER) :
          (s_is_dark ? lv_color_hex(0xF9FAFB) : lv_color_hex(0x111827)), 0);
    }
    if (s_hdr_wifi_icons[i]) {
      lv_img_set_src(s_hdr_wifi_icons[i], ic_src);
      lv_obj_set_style_img_recolor(s_hdr_wifi_icons[i], lv_color_hex(ic_color), 0);
    }
  }

  // 2. Refrescar tarjeta permanente de Wi-Fi en el panel de Ajustes de Maquina
  if (s_wifi_cur_card) {
    if (is_conn && ssid && strlen(ssid) > 0) {
      lv_obj_set_style_border_color(s_wifi_cur_card, FOCAL_COLOR_PASTEL_MINT, 0);
      lv_obj_set_style_border_width(s_wifi_cur_card, 1, 0);
      if (s_wifi_detail_icon) {
        lv_img_set_src(s_wifi_detail_icon, focal_get_wifi_icon_by_rssi(rssi));
        lv_obj_set_style_img_recolor(s_wifi_detail_icon, lv_color_hex(FOCAL_HEX_PASTEL_MINT), 0);
      }
      if (s_lbl_wifi_detail_ssid) {
        lv_label_set_text(s_lbl_wifi_detail_ssid, ssid);
        lv_obj_set_style_text_color(s_lbl_wifi_detail_ssid, FOCAL_COLOR_PASTEL_MINT, 0);
      }
      if (s_lbl_wifi_detail_sep) {
        lv_obj_clear_flag(s_lbl_wifi_detail_sep, LV_OBJ_FLAG_HIDDEN);
      }
      if (s_lbl_wifi_detail) {
        char ip_buf[48];
        snprintf(ip_buf, sizeof(ip_buf), "IP: %s", (ip && strlen(ip) > 0) ? ip : "---");
        lv_label_set_text(s_lbl_wifi_detail, ip_buf);
        lv_obj_set_style_text_color(s_lbl_wifi_detail, FOCAL_COLOR_PASTEL_MINT, 0);
      }
    } else if (is_connecting) {
      lv_obj_set_style_border_color(s_wifi_cur_card, lv_color_hex(FOCAL_HEX_PASTEL_AMBER), 0);
      lv_obj_set_style_border_width(s_wifi_cur_card, 1, 0);
      if (s_wifi_detail_icon) {
        lv_img_set_src(s_wifi_detail_icon, &focal_icon_wifi_dot);
        lv_obj_set_style_img_recolor(s_wifi_detail_icon, lv_color_hex(FOCAL_HEX_PASTEL_AMBER), 0);
      }
      if (s_lbl_wifi_detail_ssid) {
        lv_label_set_text(s_lbl_wifi_detail_ssid, (ssid && strlen(ssid) > 0) ? ssid : "Wi-Fi");
        lv_obj_set_style_text_color(s_lbl_wifi_detail_ssid, lv_color_hex(FOCAL_HEX_PASTEL_AMBER), 0);
      }
      if (s_lbl_wifi_detail_sep) {
        lv_obj_clear_flag(s_lbl_wifi_detail_sep, LV_OBJ_FLAG_HIDDEN);
      }
      if (s_lbl_wifi_detail) {
        lv_label_set_text(s_lbl_wifi_detail, "Conectando...");
        lv_obj_set_style_text_color(s_lbl_wifi_detail, lv_color_hex(FOCAL_HEX_PASTEL_AMBER), 0);
      }
    } else {
      lv_obj_set_style_border_color(s_wifi_cur_card,
          s_is_dark ? lv_color_hex(0x2D333F) : lv_color_hex(0xD1D5DB), 0);
      lv_obj_set_style_border_width(s_wifi_cur_card, 1, 0);
      if (s_wifi_detail_icon) {
        lv_img_set_src(s_wifi_detail_icon, &focal_icon_wifi_dot);
        lv_obj_set_style_img_recolor(s_wifi_detail_icon, lv_color_hex(0x9CA3AF), 0);
      }
      if (s_lbl_wifi_detail_ssid) {
        lv_label_set_text(s_lbl_wifi_detail_ssid, "Sin Wi-Fi");
        lv_obj_set_style_text_color(s_lbl_wifi_detail_ssid, lv_color_hex(0x9CA3AF), 0);
      }
      if (s_lbl_wifi_detail_sep) {
        lv_obj_add_flag(s_lbl_wifi_detail_sep, LV_OBJ_FLAG_HIDDEN);
      }
      if (s_lbl_wifi_detail) {
        lv_label_set_text(s_lbl_wifi_detail, "Desconectado");
        lv_obj_set_style_text_color(s_lbl_wifi_detail, lv_color_hex(0x9CA3AF), 0);
      }
    }
  }
}

void ui_update_dro(float wx, float wy, float wz, float mx, float my, float mz) {
  s_sim_x = wx;
  s_sim_y = wy;
  update_dro_display();
}

void ui_update_status(const char *status_str, uint32_t color_hex) {
  (void)color_hex;
  // El pill de la esquina superior derecha esta reservado exclusivamente para el estado Wi-Fi.
  ESP_LOGI("FOCAL_STATUS", "Estado firmware: %s", status_str ? status_str : "");
}

void ui_update_cut_monitor(float progress_pct, uint32_t current_line,
                           uint32_t total_lines, uint32_t elapsed_sec,
                           uint32_t power_pct, uint32_t feedrate) {
  (void)power_pct;
  (void)feedrate;
  if (s_cut_liquid_wave) {
    liquid_wave_progress_set_progress(s_cut_liquid_wave, progress_pct / 100.0f);
  }
  if (s_mon_lbl_pct) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f%%", progress_pct);
    lv_label_set_text(s_mon_lbl_pct, buf);
  }
  if (s_mon_lbl_lines) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Linea: %u / %u", (unsigned)current_line,
             (unsigned)total_lines);
    lv_label_set_text(s_mon_lbl_lines, buf);
  }
  if (s_mon_lbl_elapsed) {
    char buf[64];
    uint32_t em = elapsed_sec / 60;
    uint32_t es = elapsed_sec % 60;
    snprintf(buf, sizeof(buf), "Transcurrido: %02u:%02u min", (unsigned)em,
             (unsigned)es);
    lv_label_set_text(s_mon_lbl_elapsed, buf);
  }
  if (s_mon_lbl_remaining) {
    char buf[64];
    uint32_t rem_sec = 0;
    if (progress_pct > 0.5f && total_lines > current_line) {
      uint32_t total_est = (uint32_t)((float)elapsed_sec * 100.0f / progress_pct);
      rem_sec = (total_est > elapsed_sec) ? (total_est - elapsed_sec) : 0;
    }
    uint32_t rm = rem_sec / 60;
    uint32_t rs = rem_sec % 60;
    snprintf(buf, sizeof(buf), "Restante: %02u:%02u min", (unsigned)rm,
             (unsigned)rs);
    lv_label_set_text(s_mon_lbl_remaining, buf);
  }
  if (s_mon_lbl_filename && s_selected_file[0] != '\0') {
    lv_label_set_text(s_mon_lbl_filename, s_selected_file);
  }
}

void ui_show_cut_modal(const char *filename, uint32_t lines, uint32_t est_sec) {
  if (filename)
    strncpy(s_pending_cut_file, filename, sizeof(s_pending_cut_file) - 1);
  s_pending_lines = lines;
  s_pending_est_sec = est_sec;

  if (s_cut_modal_file_lbl) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Archivo: %s", s_pending_cut_file);
    lv_label_set_text(s_cut_modal_file_lbl, buf);
  }
  if (s_cut_modal_info_lbl) {
    char buf[80];
    uint32_t m = est_sec / 60;
    uint32_t s = est_sec % 60;
    snprintf(buf, sizeof(buf),
             "Lineas: %u  |  Duracion estimada: %02u:%02u min", (unsigned)lines,
             (unsigned)m, (unsigned)s);
    lv_label_set_text(s_cut_modal_info_lbl, buf);
  }
  if (s_cut_modal) {
    lv_obj_clear_flag(s_cut_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_cut_modal);
  }
}

void ui_close_cut_modal(void) {
  if (s_cut_modal) {
    lv_obj_add_flag(s_cut_modal, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_show_sd_delete_modal(const char *filename) {
  if (filename) {
    strncpy(s_sd_modal_target_file, filename, sizeof(s_sd_modal_target_file) - 1);
    s_sd_modal_target_file[sizeof(s_sd_modal_target_file) - 1] = '\0';
  }
  if (s_sd_del_modal) {
    if (s_sd_del_msg_lbl) {
      char msg[128];
      snprintf(msg, sizeof(msg), "Deseas eliminar permanentemente\n'%s' de la SD?", s_sd_modal_target_file);
      lv_label_set_text(s_sd_del_msg_lbl, msg);
    }
    lv_obj_clear_flag(s_sd_del_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_sd_del_modal);
  }
}

void ui_show_settings_format_modal(void) {
  show_settings_confirm_dialog(
      "Formatear SD",
      "Deseas formatear la tarjeta MicroSD?\nSe eliminaran permanentemente todos los archivos.",
      "Formatear",
      true,
      do_sd_format);
}

void ui_show_bed_dim_confirm_modal(void) {
  on_bed_dim_apply_cb(NULL);
}

void ui_test_delete_sd_file(int idx) {
  if (idx >= 0 && idx < NUM_SD_SAMPLE_FILES) {
    s_sd_modal_target_idx = idx;
    strncpy(s_sd_modal_target_file, s_sd_files[idx], sizeof(s_sd_modal_target_file) - 1);
    s_sd_modal_target_file[sizeof(s_sd_modal_target_file) - 1] = '\0';
    on_sd_del_confirm_cb(NULL);
  }
}

void ui_show_calib_success_modal(void) {
  if (s_calib_success_modal) {
    lv_obj_clear_flag(s_calib_success_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_calib_success_modal);
  }
}

