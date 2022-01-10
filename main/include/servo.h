#ifndef MAIN_INCLUDE_SERVO_H_
#define MAIN_INCLUDE_SERVO_H_

#include "i2cdev.h"
#include "pca9685.h"
#include "global.h"

#define OFF             0
#define ON              4095

esp_err_t init_servo();
i2c_dev_t *get_i2c_dev();
void set_freq(int freq);
esp_err_t create_servo(pca9685_channel_t pin);
esp_err_t create_servo_steering(pca9685_channel_t pin);
void delete_servo(pca9685_channel_t pin);
/*position -1 step left, 181 step right, 0-180 degree */
void new_position_servo(pca9685_channel_t pin, int16_t position);
void set_servo_reverse(pca9685_channel_t, bool reverse);
long map(long x, long in_min, long in_max, long out_min, long out_max);

#endif /* MAIN_INCLUDE_SERVO_H_ */
