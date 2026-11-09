/**
 * @file mock_wifi_manager.c
 * @brief Realistic Simulated Wi-Fi Stack for Focal CNC Desktop Simulator
 */

#include "wifi_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SIM_WIFI";

static bool s_sta_connected = false;
static bool s_sta_connecting = false;
static char s_sta_ssid[33] = "";
static char s_sta_ip[20] = "Desconectado";
static int8_t s_sta_rssi = 0;

void wifi_manager_init(void)
{
    ESP_LOGI(TAG, "Wi-Fi Simulator inicializado. Red local activa.");
}

bool wifi_manager_is_connected(void)
{
    return s_sta_connected;
}

const char *wifi_manager_get_ap_ip(void)
{
    return "0.0.0.0";
}

const char *wifi_manager_get_ip(void)
{
    return s_sta_connected ? s_sta_ip : "Desconectado";
}

esp_err_t wifi_manager_scan(wifi_scan_item_t *out_list, uint16_t max_items, uint16_t *out_count)
{
    ESP_LOGI(TAG, "Escaneando el entorno de radio (simulado)...");

    static const wifi_scan_item_t mock_aps[] = {
        { .ssid = "TIGO032125",       .rssi = -42, .authmode = 3 },
        { .ssid = "Mi_Casa_5G",        .rssi = -51, .authmode = 3 },
        { .ssid = "Taller_CNC_2.4G",   .rssi = -64, .authmode = 3 },
        { .ssid = "Red_Vecino_Fibra",  .rssi = -78, .authmode = 3 },
        { .ssid = "Invitados_Taller",  .rssi = -85, .authmode = 0 },
    };

    uint16_t count = sizeof(mock_aps) / sizeof(mock_aps[0]);
    if (count > max_items) count = max_items;

    for (uint16_t i = 0; i < count; i++) {
        out_list[i] = mock_aps[i];
    }
    *out_count = count;

    ESP_LOGI(TAG, "Escaneo finalizado: %d redes encontradas.", count);
    return ESP_OK;
}

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Conectando a red Wi-Fi: '%s'...", ssid);
    strncpy(s_sta_ssid, ssid, sizeof(s_sta_ssid) - 1);
    s_sta_ssid[sizeof(s_sta_ssid) - 1] = '\0';

    s_sta_connected = true;
    s_sta_rssi = -45;
    snprintf(s_sta_ip, sizeof(s_sta_ip), "192.168.1.%d", 100 + (ssid[0] % 50));
    ESP_LOGI(TAG, "Conexion exitosa a '%s'. IP asignada: %s (RSSI: %d dBm)", s_sta_ssid, s_sta_ip, s_sta_rssi);
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect_sta(void)
{
    ESP_LOGW(TAG, "Desconectado de la red Wi-Fi.");
    s_sta_connected = false;
    strcpy(s_sta_ip, "Desconectado");
    s_sta_rssi = -100;
    return ESP_OK;
}

bool wifi_manager_is_sta_connected(void)
{
    return s_sta_connected;
}

bool wifi_manager_is_sta_connecting(void)
{
    return s_sta_connecting;
}

const char *wifi_manager_get_sta_ip(void)
{
    return s_sta_connected ? s_sta_ip : "Desconectado";
}

const char *wifi_manager_get_sta_ssid(void)
{
    return s_sta_connected ? s_sta_ssid : "Desconectado";
}

int8_t wifi_manager_get_sta_rssi(void)
{
    return s_sta_rssi;
}

void sim_wifi_toggle_connection(void)
{
    s_sta_connected = !s_sta_connected;
    if (s_sta_connected) {
        strcpy(s_sta_ip, "192.168.1.105");
        s_sta_rssi = -48;
        ESP_LOGI(TAG, "[SIMULADOR] Wi-Fi reconectado a '%s' (IP: %s)", s_sta_ssid, s_sta_ip);
    } else {
        strcpy(s_sta_ip, "Desconectado");
        s_sta_rssi = -100;
        ESP_LOGW(TAG, "[SIMULADOR] Wi-Fi desconectado");
    }
}
