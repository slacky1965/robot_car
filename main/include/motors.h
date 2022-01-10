#ifndef MAIN_INCLUDE_MOTORS_H_
#define MAIN_INCLUDE_MOTORS_H_

esp_err_t create_motor();
void delete_motor();
void motor_go_forward();
void motor_go_back();
void motor_stop();
void motor_go_left();
void motor_go_right();
void motor_go_straight();
void motor_set_speed(int16_t speed);
void motor_left_set_speed(int16_t speed);
void motor_right_set_speed(int16_t speed);

#endif /* MAIN_INCLUDE_MOTORS_H_ */
