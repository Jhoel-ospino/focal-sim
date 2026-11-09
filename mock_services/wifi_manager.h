/**
 * @file wifi_manager.h
 * @brief Simulated Wi-Fi Manager for Focal CNC Digital Twin / Simulator
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_SCAN_AP 16

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_scan_item_t;

void wifi_manager_init(void);
bool wifi_manager_is_connected(void);
const char *wifi_manager_get_ap_ip(void);
const char *wifi_manager_get_ip(void);
esp_err_t wifi_manager_scan(wifi_scan_item_t *out_list, uint16_t max_items, uint16_t *out_count);
esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password);
esp_err_t wifi_manager_disconnect_sta(void);
bool wifi_manager_is_sta_connected(void);
bool wifi_manager_is_sta_connecting(void);
const char *wifi_manager_get_sta_ip(void);
const char *wifi_manager_get_sta_ssid(void);
int8_t wifi_manager_get_sta_rssi(void);

// Simulator helper to toggle Wi-Fi connection from keyboard shortcut
void sim_wifi_toggle_connection(void);

#ifdef __cplusplus
}
#endif
