#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void laser_set_enable(bool en);
void laser_set_power(uint16_t s_val);

#ifdef __cplusplus
}
#endif
