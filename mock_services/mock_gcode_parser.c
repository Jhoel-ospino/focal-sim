#include "gcode_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SIM_GCODE";

esp_err_t gcode_parser_execute_line(const char *line, char *out_response, size_t max_resp_len) {
    ESP_LOGI(TAG, "G-Code Exec: '%s'", line);
    if (out_response && max_resp_len > 0) {
        if (strcmp(line, "?") == 0) {
            snprintf(out_response, max_resp_len, "<Idle|MPos:0.000,0.000,0.000|FS:0,0|Pn:P>");
        } else {
            snprintf(out_response, max_resp_len, "ok");
        }
    }
    return ESP_OK;
}
