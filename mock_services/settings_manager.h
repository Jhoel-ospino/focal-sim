/**
 * @file settings_manager.h
 * @brief Header for simulated settings manager
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_MAX_SAVED_WIFI 5

typedef struct {
    char ssid[32];
    char pass[64];
} wifi_saved_ap_t;

typedef struct {
    // 1. Conectividad Wi-Fi
    char wifi_ssid[32];
    char wifi_pass[64];
    bool wifi_sta_enabled;
    wifi_saved_ap_t saved_wifis[SETTINGS_MAX_SAVED_WIFI];
    uint8_t saved_wifis_count;

    // 2. Gestión de Archivos en SD
    bool sd_auto_overwrite;
    uint8_t sd_sort_mode;
    char sd_default_folder[32];

    // 3. Parámetros del Módulo Láser
    uint16_t laser_max_s;
    uint16_t laser_pwm_freq;
    bool laser_air_assist;
    uint16_t laser_delay_ms;
    uint8_t laser_test_power;

    // 4. Parámetros Cinemáticos y Límites (GRBL)
    float steps_per_mm_x;
    float steps_per_mm_y;
    float max_rate_x;
    float max_rate_y;
    float accel_x;
    float accel_y;
    float travel_max_x;
    float travel_max_y;
    int16_t bed_rotation;

    // 5. Calibración Óptica
    float focal_z_height;

    // 6. Apariencia y Tema de UI
    uint8_t theme_mode;          // 0: Modo Oscuro, 1: Modo Claro
    uint8_t accent_color_idx;    // 0: Blue, 1: Mint, 2: Amber, 3: Purple, 4: Red

    // 7. Favoritos de Corte Rapido
    char favorite_files[2][64];
    uint8_t num_favorites;
} machine_settings_t;

esp_err_t settings_manager_init(void);
void settings_manager_get(machine_settings_t *out_settings);
esp_err_t settings_manager_save(const machine_settings_t *settings);
esp_err_t settings_manager_reset_defaults(void);
esp_err_t settings_manager_save_wifi_credentials(const char *ssid, const char *pass);
bool settings_manager_find_wifi(const char *ssid, char *out_pass, size_t max_len);
void settings_manager_add_or_update_wifi(const char *ssid, const char *pass);
void settings_manager_forget_current_wifi(void);

#ifdef __cplusplus
}
#endif
