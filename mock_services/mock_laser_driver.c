#include "laser_driver.h"
#include "esp_log.h"

static const char *TAG = "SIM_LASER";
static bool s_en = false;
static uint16_t s_power = 0;

void laser_set_enable(bool en) {
    s_en = en;
    ESP_LOGD(TAG, "Laser Enable: %s", en ? "ON" : "OFF");
}

void laser_set_power(uint16_t s_val) {
    s_power = s_val;
    if (s_val > 0) {
        ESP_LOGI(TAG, "Laser Power S=%u (%.1f%%)", s_val, (s_val / 1000.0f) * 100.0f);
    }
}
