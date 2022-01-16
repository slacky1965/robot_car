#ifndef MAIN_INCLUDE_PULSE_H_
#define MAIN_INCLUDE_PULSE_H_

#include "config.h"

esp_err_t init_pulse();
void deinit_pulse();
void get_speed_time(uint64_t *speed_left, uint64_t *speed_right);

#endif /* MAIN_INCLUDE_PULSE_H_ */
