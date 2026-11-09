/**
 * @file ui_screens.h
 * @brief Industrial UI Screen Manager for Focal CNC Laser Cutter
 *
 * Implements Apple-like macOS/iPadOS minimalist dark aesthetic,
 * 3x2 Grid Springboard Launcher, Jogging, SD Manager, Live Cut Monitor,
 * Multi-Option Calibration Submenu, Shape Cutting with Bed Locator,
 * and Persistent Settings with on-screen alphanumeric keyboard.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_LAUNCHER = 0,     // Menú de inicio en grilla 3x2 de iconos/tarjetas
    UI_SCREEN_JOGGING,          // Control de máquina (DRO, movimientos X/Y, pasos)
    UI_SCREEN_SD_FILES,         // Gestor y explorador de archivos en SD
    UI_SCREEN_CUT_MONITOR,      // Monitor de corte y telemetría en tiempo real
    UI_SCREEN_CALIBRATION,      // Submenú de calibración (Home, Enfoque, Cama, Láser, Pasos)
    UI_SCREEN_SHAPES,           // Corte de figuras preestablecidas con ubicación en cama
    UI_SCREEN_SETTINGS,         // Configuración (Wi-Fi + Teclado, SD, Láser, Flash NVS, Reset)
    UI_SCREEN_BED_DIMENSIONS,   // Editor de dimensiones y rotación de cama de corte
    UI_SCREEN_MAX
} ui_screen_id_t;

/**
 * @brief Inicializa el subsistema de interfaz gráfica LVGL con tokens de Focal.
 *        Inicia directamente en el Launcher 3x2.
 */
void ui_init(void);

/**
 * @brief Cambia de pantalla activa con animación suave.
 */
void ui_switch_screen(ui_screen_id_t screen_id);

/**
 * @brief Obtiene el ID de la pantalla actualmente visible.
 */
ui_screen_id_t ui_get_current_screen(void);

/**
 * @brief Establece el estado de ejecución de corte para bloqueos de seguridad.
 */
void ui_set_cutting_state(bool is_cutting);

/**
 * @brief Consulta si la máquina se encuentra ejecutando un corte activo.
 */
bool ui_is_cutting(void);

/**
 * @brief Activa o desactiva el modo de demostración automática.
 */
void ui_set_demo_mode(bool enable);

/**
 * @brief Actualiza las lecturas de coordenadas DRO en la pantalla de jogging.
 */
void ui_update_dro(float wx, float wy, float wz, float mx, float my, float mz);

/**
 * @brief Actualiza la píldora de estado global en el encabezado.
 * @param status_str Texto (e.g. "IDLE", "RUN", "HOLD", "ALARM")
 * @param color_hex Color pastel representativo (e.g. FOCAL_HEX_PASTEL_MINT)
 */
void ui_update_status(const char *status_str, uint32_t color_hex);

/**
 * @brief Actualiza los datos de telemetría en el monitor de corte.
 */
void ui_update_cut_monitor(float progress_pct, uint32_t current_line, uint32_t total_lines, 
                           uint32_t elapsed_sec, uint32_t power_pct, uint32_t feedrate);

/**
 * @brief Muestra el diálogo modal de confirmación de corte para un archivo seleccionado.
 */
void ui_show_cut_modal(const char *filename, uint32_t lines, uint32_t est_sec);

/**
 * @brief Cierra el diálogo modal si está visible.
 */
void ui_close_cut_modal(void);
void ui_set_cutting_state(bool is_cutting);
bool ui_is_cutting(void);
void ui_show_stop_modal(bool show);
void ui_update_wifi_display(void);
void ui_settings_select_tab(int tab_idx);
void ui_show_wifi_modal(const char *ssid);
void ui_hide_wifi_modal(void);
void ui_calibration_set_step(int step_idx);
void ui_shapes_set_preset(int preset_idx);
void ui_shapes_nudge(int dx, int dy);
void ui_shapes_rotate(int delta_deg);
void ui_shapes_select_shape(int shape_idx);
void ui_shapes_select_favorite(int fav_idx);
void ui_add_favorite_file(const char *filename);
void ui_show_sd_file_options_modal(const char *filename, int file_idx);
void ui_hide_sd_file_options_modal(void);
void ui_set_bed_dimensions(int w_mm, int h_mm, int rotation_deg);
void ui_get_bed_dimensions(int *w_mm, int *h_mm, int *rotation_deg);
const lv_img_dsc_t *focal_get_wifi_icon_by_rssi(int rssi);
void ui_show_legal_modal(void);
void ui_hide_legal_modal(void);
void ui_show_sd_delete_modal(const char *filename);
void ui_show_settings_format_modal(void);
void ui_show_bed_dim_confirm_modal(void);
void ui_test_delete_sd_file(int idx);
void ui_show_calib_success_modal(void);

#ifdef __cplusplus
}
#endif
