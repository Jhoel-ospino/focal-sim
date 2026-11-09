/**
 * @file mock_settings_manager.c
 * @brief Persistent Settings Manager for Focal CNC Desktop Simulator
 */

#include "settings_manager.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "SIM_SETTINGS";
static const char *SETTINGS_FILE = "simulator_settings.bin";

static machine_settings_t s_cfg = {
    .wifi_ssid = "",
    .wifi_pass = "",
    .wifi_sta_enabled = false,
    .saved_wifis = { { "", "" } },
    .saved_wifis_count = 0,

    .sd_auto_overwrite = true,
    .sd_sort_mode = 0,
    .sd_default_folder = "/gcode",

    .laser_max_s = 1000,
    .laser_pwm_freq = 5000,
    .laser_air_assist = true,
    .laser_delay_ms = 50,
    .laser_test_power = 1,

    .steps_per_mm_x = 400.0f,
    .steps_per_mm_y = 400.0f,
    .max_rate_x = 6000.0f,
    .max_rate_y = 6000.0f,
    .accel_x = 500.0f,
    .accel_y = 500.0f,
    .travel_max_x = 200.0f,
    .travel_max_y = 160.0f,
    .bed_rotation = 0,

    .focal_z_height = 30.0f,

    .theme_mode = 0, // Dark
    .accent_color_idx = 0, // Blue

    .favorite_files = { "", "" },
    .num_favorites = 0
};

esp_err_t settings_manager_init(void)
{
    FILE *f = fopen(SETTINGS_FILE, "rb");
    if (f) {
        fread(&s_cfg, sizeof(machine_settings_t), 1, f);
        fclose(f);
        ESP_LOGI(TAG, "Configuraciones cargadas de archivo local '%s'", SETTINGS_FILE);
    } else {
        ESP_LOGI(TAG, "Inicializado con configuraciones por defecto (Cama 160x200 mm)");
    }
    return ESP_OK;
}

void settings_manager_get(machine_settings_t *out_settings)
{
    if (out_settings) {
        *out_settings = s_cfg;
    }
}

esp_err_t settings_manager_save(const machine_settings_t *settings)
{
    if (!settings) return ESP_FAIL;
    s_cfg = *settings;
    FILE *f = fopen(SETTINGS_FILE, "wb");
    if (f) {
        fwrite(&s_cfg, sizeof(machine_settings_t), 1, f);
        fclose(f);
    }
    ESP_LOGI(TAG, "Configuracion guardada (Tema: %s, Color: %d)",
             s_cfg.theme_mode == 0 ? "Oscuro" : "Claro", s_cfg.accent_color_idx);
    return ESP_OK;
}

esp_err_t settings_manager_reset_defaults(void)
{
    s_cfg.travel_max_x = 200.0f;
    s_cfg.travel_max_y = 160.0f;
    s_cfg.bed_rotation = 0;
    s_cfg.theme_mode = 0;
    s_cfg.accent_color_idx = 0;
    settings_manager_save(&s_cfg);
    ESP_LOGI(TAG, "Configuraciones restauradas a valores de fabrica.");
    return ESP_OK;
}

esp_err_t settings_manager_save_wifi_credentials(const char *ssid, const char *pass)
{
    if (!ssid) return ESP_FAIL;
    strncpy(s_cfg.wifi_ssid, ssid, sizeof(s_cfg.wifi_ssid) - 1);
    if (pass) strncpy(s_cfg.wifi_pass, pass, sizeof(s_cfg.wifi_pass) - 1);
    s_cfg.wifi_sta_enabled = true;
    settings_manager_add_or_update_wifi(ssid, pass ? pass : "");
    return settings_manager_save(&s_cfg);
}

bool settings_manager_find_wifi(const char *ssid, char *out_pass, size_t max_len)
{
    if (!ssid || ssid[0] == '\0') return false;
    for (int i = 0; i < s_cfg.saved_wifis_count && i < SETTINGS_MAX_SAVED_WIFI; i++) {
        if (strcmp(s_cfg.saved_wifis[i].ssid, ssid) == 0) {
            if (out_pass && max_len > 0) {
                strncpy(out_pass, s_cfg.saved_wifis[i].pass, max_len - 1);
                out_pass[max_len - 1] = '\0';
            }
            return true;
        }
    }
    return false;
}

void settings_manager_add_or_update_wifi(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') return;
    for (int i = 0; i < s_cfg.saved_wifis_count && i < SETTINGS_MAX_SAVED_WIFI; i++) {
        if (strcmp(s_cfg.saved_wifis[i].ssid, ssid) == 0) {
            if (pass) {
                strncpy(s_cfg.saved_wifis[i].pass, pass, sizeof(s_cfg.saved_wifis[i].pass) - 1);
            }
            settings_manager_save(&s_cfg);
            return;
        }
    }

    if (s_cfg.saved_wifis_count < SETTINGS_MAX_SAVED_WIFI) {
        int idx = s_cfg.saved_wifis_count++;
        strncpy(s_cfg.saved_wifis[idx].ssid, ssid, sizeof(s_cfg.saved_wifis[idx].ssid) - 1);
        if (pass) {
            strncpy(s_cfg.saved_wifis[idx].pass, pass, sizeof(s_cfg.saved_wifis[idx].pass) - 1);
        } else {
            s_cfg.saved_wifis[idx].pass[0] = '\0';
        }
    } else {
        // Shift left and replace last
        for (int i = 0; i < SETTINGS_MAX_SAVED_WIFI - 1; i++) {
            s_cfg.saved_wifis[i] = s_cfg.saved_wifis[i + 1];
        }
        int idx = SETTINGS_MAX_SAVED_WIFI - 1;
        strncpy(s_cfg.saved_wifis[idx].ssid, ssid, sizeof(s_cfg.saved_wifis[idx].ssid) - 1);
        if (pass) {
            strncpy(s_cfg.saved_wifis[idx].pass, pass, sizeof(s_cfg.saved_wifis[idx].pass) - 1);
        } else {
            s_cfg.saved_wifis[idx].pass[0] = '\0';
        }
    }
    settings_manager_save(&s_cfg);
}

void settings_manager_forget_current_wifi(void)
{
    if (s_cfg.wifi_ssid[0] == '\0') return;
    for (int i = 0; i < s_cfg.saved_wifis_count; i++) {
        if (strcmp(s_cfg.saved_wifis[i].ssid, s_cfg.wifi_ssid) == 0) {
            for (int j = i; j < s_cfg.saved_wifis_count - 1; j++) {
                s_cfg.saved_wifis[j] = s_cfg.saved_wifis[j + 1];
            }
            s_cfg.saved_wifis_count--;
            break;
        }
    }
    s_cfg.wifi_ssid[0] = '\0';
    s_cfg.wifi_pass[0] = '\0';
    s_cfg.wifi_sta_enabled = false;
    settings_manager_save(&s_cfg);
}
