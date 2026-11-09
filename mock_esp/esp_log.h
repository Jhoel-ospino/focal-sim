#pragma once

#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_LOG_COLOR_BLACK   "30"
#define ESP_LOG_COLOR_RED     "31"
#define ESP_LOG_COLOR_GREEN   "32"
#define ESP_LOG_COLOR_BROWN   "33"
#define ESP_LOG_COLOR_BLUE    "34"
#define ESP_LOG_COLOR_PURPLE  "35"
#define ESP_LOG_COLOR_CYAN    "36"
#define ESP_LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define ESP_LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define ESP_LOG_RESET_COLOR   "\033[0m"

#define ESP_LOGE(tag, format, ...) printf(ESP_LOG_COLOR(ESP_LOG_COLOR_RED)   "E [%s] " format ESP_LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf(ESP_LOG_COLOR(ESP_LOG_COLOR_BROWN) "W [%s] " format ESP_LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) printf(ESP_LOG_COLOR(ESP_LOG_COLOR_GREEN) "I [%s] " format ESP_LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) printf(ESP_LOG_COLOR(ESP_LOG_COLOR_CYAN)  "D [%s] " format ESP_LOG_RESET_COLOR "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) printf("V [%s] " format "\n", tag, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
