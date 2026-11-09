#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gcode_parser_execute_line(const char *line, char *out_response, size_t max_resp_len);

#ifdef __cplusplus
}
#endif
