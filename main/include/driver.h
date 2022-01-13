#ifndef MAIN_INCLUDE_DRIVER_H_
#define MAIN_INCLUDE_DRIVER_H_

esp_err_t init_driver();
void deinit_driver();

void automatic_car(bool automatic);
void turn_left_car();
void turn_right_car();
void turn_stop_car();
void forward_start_car();
void forward_stop_car();
void back_start_car();
void back_stop_car();
void stop_car();
void set_speed_car(int16_t speed);
esp_err_t get_status_car(cJSON **root);

#endif /* MAIN_INCLUDE_DRIVER_H_ */
