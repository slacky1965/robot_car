#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "cJSON.h"
#include "driver/mcpwm.h"
#include "soc/mcpwm_periph.h"

#include "driver.h"
#include "pulse.h"

#define LOW                 0
#define HIGH                1

#define ANGLE_MIN           0
#define ANGLE_MAX           180
#define STEERING_ANGLE_MIN  45              /* 90 -> 45  left turn                  */
#define STEERING_ANGLE_MAX  135             /* 90 -> 135 right turn                 */
#define STEERING_STRAIGHT   90
#define NOTHING             255
#define SERVO_MIN_US        504
#define SERVO_MAX_US        2360
#define STEERING_DELAY      5               /* turning speed steering servo         */
#define STEERING_STEP       5
#define STEERING_CENTER     0               /* correction for straight of steering in degrees . Example -5 or 10 */
#define SERVO_PWM_GPIO      GPIO_NUM_21     /* channel steering                     */

#define LEFT_MOTOR_GPIO_1   GPIO_NUM_16     /* First power GPIO of left motor       */
#define LEFT_MOTOR_GPIO_2   GPIO_NUM_17     /* Second power GPIO of left motor      */
#define RIGHT_MOTOR_GPIO_1  GPIO_NUM_18     /* First power GPIO of right motor      */
#define RIGHT_MOTOR_GPIO_2  GPIO_NUM_19     /* Second power GPIO of right motor     */
#define LEFT_SPD_PWM_GPIO   GPIO_NUM_22     /* GPIO for left motor speed variation  */
#define RIGHT_SPD_PWM_GPIO  GPIO_NUM_23     /* GPIO for right motor speed variation */

#define SPEED_MIN           1               /* speed 1-255 map to 700-5000 */
#define SPEED_MAX           255
#define VAL_SPEED_MIN       700
#define VAL_SPEED_MAX       5000
#define SPEED_TURN_STEP     70
#define SPEEDUP_STEP        100

/*
 *      cmd_no command -         no command
 *
 *      cmd_turn_left command -  smooth left turn
 *
 *      cmd_turn_right command - smooth right turn
 *
 *      cmd_turn_stop command -  stop further turning the steering wheel
 *
 *      cmd_speedup command -    smooth increase in speed
 *
 *      cmd_slowdown command -   smooth decrease in speed
 *
 *      cmd_speedstop command -  stop changing speed
 *
 *      cmd_forward command -    if back - smooth decrease in speed
 *                               if forward - smooth increase in speed
 *                               if left or right - straight
 *
 *      cmd_back command -       if forward - smooth decrease in speed
 *                               if back - smooth increase in speed
 *                               if left or right - straight
 *
 */
enum {
    cmd_no =         0b00000000,
    cmd_turn_stop =  0b00000001,
    cmd_turn_left =  0b00000010,
    cmd_turn_right = 0b00000100,
    cmd_speedup =    0b00001000,
    cmd_slowdown =   0b00010000,
    cmd_speedstop =  0b00100000,
    cmd_forward =    0b01000000,
    cmd_back =       0b10000000
};

/*
 *  rear wheel drive status
 *
 *      motors_stop status -    the motors are stopped
 *
 *      motors_forward status - motors forward
 *
 *      motors_back status -    motors back
 *
 */
typedef enum {
    motors_stop =    0b00000001,
    motors_forward = 0b00000010,
    motors_back =    0b00000100
} motors_status_t;

typedef struct {
    int                 gpio_num;
    mcpwm_io_signals_t  io_signal;
    mcpwm_unit_t        unit;
    mcpwm_timer_t       timer;
    mcpwm_generator_t   gen;
    mcpwm_config_t      pwm_config;
} mcpwm_t;

typedef struct {
    mcpwm_t         mcpwm;
    int16_t         current_position;
    int16_t         correction_center;
    int16_t         degree_min;
    int16_t         degree_max;
    uint32_t        duty_min_us;
    uint32_t        duty_max_us;
    uint32_t        delay;
    bool            exit_task;
} servomotor_t;

typedef struct {
    int             gpio_motor_plus;
    uint8_t         value_motor_plus;
    int             gpio_motor_minus;
    uint8_t         value_motor_minus;
    mcpwm_t         pwm_speed;
    int16_t         value_speed;
    int16_t         new_value_speed;
    int16_t         correction_speed;
} motor_side_t;

typedef struct {
    motor_side_t    motor_left;
    motor_side_t    motor_right;
    uint32_t        duty_min_us;
    uint32_t        duty_max_us;
    int16_t         turn;
    motors_status_t status;
    TaskHandle_t    handler_speed_correction_task;
} motors_t;

typedef struct {
    servomotor_t   *steering;
    motors_t       *motors;
    TaskHandle_t    handler_driver_task;
    QueueHandle_t   queue_driver;
} driver_t;

static char *TAG = "robot_car_driver";

driver_t *driver_car = NULL;

/* ============================================================================================= */
/*                                      Private zone                                             */
/* ============================================================================================= */


long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static uint32_t angle_to_us(uint32_t angle, uint32_t min_us, uint32_t max_us) {
    uint32_t us = ((max_us - min_us) / ANGLE_MAX) * angle + min_us;
    return us;
}

static esp_err_t set_driver_pwm_us(mcpwm_t *mcpwm, uint32_t us) {

    return mcpwm_set_duty_in_us(mcpwm->unit, mcpwm->timer, mcpwm->gen, us);

}

//static esp_err_t set_driver_pwm(mcpwm_t *mcpwm, float duty) {
//
//    return mcpwm_set_duty(mcpwm->unit, mcpwm->timer, mcpwm->gen, duty);
//
//}

static void steering_task(void *pvParameter) {
    uint32_t us;
    int16_t *degree = (int16_t*)pvParameter;

    driver_car->steering->exit_task = false;

    mcpwm_set_duty_type(driver_car->steering->mcpwm.unit, driver_car->steering->mcpwm.timer, driver_car->steering->mcpwm.gen, MCPWM_DUTY_MODE_0);

    if (driver_car->steering->current_position > *degree) {
        for (int i = driver_car->steering->current_position; i >= *degree; i--) {
            if (driver_car->steering->exit_task) {
                driver_car->steering->current_position = i;
                vTaskDelete(NULL);
            }
            us = angle_to_us(i+driver_car->steering->correction_center, driver_car->steering->duty_min_us, driver_car->steering->duty_max_us);
            set_driver_pwm_us(&(driver_car->steering->mcpwm), us);
            vTaskDelay(driver_car->steering->delay/portTICK_PERIOD_MS);
        }
    } else  if (driver_car->steering->current_position < *degree){
        for (int i = driver_car->steering->current_position; i <= *degree; i++) {
            if (driver_car->steering->exit_task) {
                driver_car->steering->current_position = i;
                vTaskDelete(NULL);
            }
            us = angle_to_us(i+driver_car->steering->correction_center, driver_car->steering->duty_min_us, driver_car->steering->duty_max_us);
            set_driver_pwm_us(&(driver_car->steering->mcpwm), us);
            vTaskDelay(driver_car->steering->delay/portTICK_PERIOD_MS);
        }
    }

    if (driver_car->steering->current_position != *degree) {
        vTaskDelay(30/portTICK_PERIOD_MS);
        driver_car->steering->current_position = *degree;
        ESP_LOGI(TAG, "Steering position - %d", driver_car->steering->current_position);
    }


    mcpwm_set_signal_low(driver_car->steering->mcpwm.unit, driver_car->steering->mcpwm.timer, driver_car->steering->mcpwm.gen);

    vTaskDelete(NULL);
}

static void set_steering(int16_t args) {

    static int16_t degree = 0;

    driver_car->steering->exit_task = true;
    vTaskDelay(10/portTICK_PERIOD_MS);

    degree = args;

    if (degree < driver_car->steering->degree_min) degree = driver_car->steering->degree_min;
    if (degree > driver_car->steering->degree_max) degree = driver_car->steering->degree_max;

    xTaskCreate(&steering_task, "steering_task", 2048, &(degree), 0, NULL);

}

static void set_motors(motors_t *motors) {


    gpio_set_level(motors->motor_left.gpio_motor_plus, motors->motor_left.value_motor_plus);
    gpio_set_level(motors->motor_left.gpio_motor_minus, motors->motor_left.value_motor_minus);
    gpio_set_level(motors->motor_right.gpio_motor_plus, motors->motor_right.value_motor_plus);
    gpio_set_level(motors->motor_right.gpio_motor_minus, motors->motor_right.value_motor_minus);


    set_driver_pwm_us(&(motors->motor_left.pwm_speed), motors->motor_left.value_speed);
    set_driver_pwm_us(&(motors->motor_right.pwm_speed), motors->motor_right.value_speed);

}

//static bool speed_correction_need(uint64_t left, uint64_t right) {
//    float fleft, fright;
//    uint64_t result;
//
//    if (left == right) return false;
//
//    fleft = left;
//    fright = right;
//
//    if (left > right) {
//        result = round(fabs(fright/fleft*100-100)*100);
//    } else {
//        result = round(fabs(fleft/fright*100-100)*100);
//    }
//
//    printf("float result - %lld\n", result);
//
//    /* return true if more than 0.25% difference */
//    if (result <= 25) return false;
//
//    return true;
//}


static void speed_correction_task(void *pvParameter) {
    uint64_t speed_left, speed_right, result;
    float fleft, fright;
    motors_t *motors = (motors_t*)pvParameter;

    while(1) {

        if (motors->turn == STEERING_STRAIGHT && !(motors->status & motors_stop)) {

            get_speed_time(&speed_left, &speed_right);

            if (speed_left != 0 && speed_right != 0) {
                fleft = speed_left;
                fright = speed_right;
                if (speed_left > speed_right) {
                    /* percentage difference */
                    result = round(fabs(fright/fleft*100-100)*100);
                    /* true if more than 0.25% difference */
                    if (result > 25) {
                        if (motors->motor_right.correction_speed != 0) {
                            motors->motor_right.correction_speed = 0;
                            set_driver_pwm_us(&(motors->motor_right.pwm_speed), motors->motor_right.value_speed);
                        } else {
                            motors->motor_left.correction_speed +=10;
                            set_driver_pwm_us(&(motors->motor_left.pwm_speed),
                                                motors->motor_left.value_speed-(motors->motor_left.correction_speed));
                        }
                    }
                } else if (speed_left < speed_right) {
                    /* percentage difference */
                    result = round(fabs(fleft/fright*100-100)*100);
                    /* true if more than 0.25% difference */
                    if (result > 25) {
                        if (motors->motor_left.correction_speed != 0) {
                            motors->motor_left.correction_speed = 0;
                            set_driver_pwm_us(&(motors->motor_left.pwm_speed), motors->motor_left.value_speed);
                        } else {
                            motors->motor_right.correction_speed +=10;
                            set_driver_pwm_us(&(motors->motor_right.pwm_speed),
                                                motors->motor_right.value_speed-(motors->motor_right.correction_speed));

                        }
                    }
                }
//                if (speed_correction_need(speed_left, speed_right)) {
//                }
            }

            printf("left_speed_time - %lld,  right_speed_time - %lld\n", speed_left, speed_right);
        }


        vTaskDelay(50/portTICK_PERIOD_MS);
    }
}

static void driver_task(void *pvParameter) {

    int16_t command = cmd_no;
    int16_t cmd_speed = cmd_no;

    while(1) {

        xQueueReceive(driver_car->queue_driver, &command, (TickType_t)driver_car->steering->delay);

        if (command & cmd_speedstop) {
            cmd_speed = command = cmd_no;
            ESP_LOGI(TAG, "Speed of left motor - %d", driver_car->motors->motor_left.new_value_speed);
            ESP_LOGI(TAG, "Speed of right motor - %d", driver_car->motors->motor_right.new_value_speed);
        }

        if (command & (cmd_speedup|cmd_slowdown)) {
            cmd_speed = command;
        }

        if (cmd_speed & cmd_speedup) {
            if (driver_car->motors->motor_left.value_speed+SPEEDUP_STEP > driver_car->motors->duty_max_us) {
                driver_car->motors->motor_left.new_value_speed = driver_car->motors->duty_max_us;
            } else {
                driver_car->motors->motor_left.new_value_speed += SPEEDUP_STEP;
            }

            if (driver_car->motors->motor_right.value_speed+SPEEDUP_STEP > driver_car->motors->duty_max_us) {
                driver_car->motors->motor_right.new_value_speed = driver_car->motors->duty_max_us;
            } else {
                driver_car->motors->motor_right.new_value_speed += SPEEDUP_STEP;
            }
        }

        if (cmd_speed & cmd_slowdown) {
            if (driver_car->motors->motor_left.value_speed-SPEEDUP_STEP < driver_car->motors->duty_min_us) {
                driver_car->motors->motor_left.new_value_speed = driver_car->motors->duty_min_us;
            } else {
                driver_car->motors->motor_left.new_value_speed -= SPEEDUP_STEP;
            }

            if (driver_car->motors->motor_right.value_speed-SPEEDUP_STEP < driver_car->motors->duty_min_us) {
                driver_car->motors->motor_right.new_value_speed = driver_car->motors->duty_min_us;
            } else {
                driver_car->motors->motor_right.new_value_speed -= SPEEDUP_STEP;
            }
        }

        if (command & cmd_turn_left) {
            if (driver_car->motors->turn <= STEERING_STRAIGHT) {
                if (driver_car->motors->turn != STEERING_ANGLE_MIN) {
                    driver_car->motors->turn -= STEERING_STEP;
                    driver_car->motors->motor_left.value_speed -= SPEED_TURN_STEP;
                    if (driver_car->motors->motor_left.value_speed < 0) driver_car->motors->motor_left.value_speed = 0;
                    driver_car->motors->motor_left.new_value_speed = driver_car->motors->motor_left.value_speed;
                    ESP_LOGI(TAG, "Speed of left motor - %d", driver_car->motors->motor_left.value_speed);
                }
            } else if (driver_car->motors->turn > STEERING_STRAIGHT) {
                driver_car->motors->turn -= STEERING_STEP;
                driver_car->motors->motor_right.value_speed += SPEED_TURN_STEP;
                if (driver_car->motors->motor_right.value_speed > driver_car->motors->duty_max_us) driver_car->motors->motor_right.value_speed = driver_car->motors->duty_max_us;
                driver_car->motors->motor_right.new_value_speed = driver_car->motors->motor_right.value_speed;
                ESP_LOGI(TAG, "Speed of right motor - %d", driver_car->motors->motor_right.value_speed);
            }
            set_steering(driver_car->motors->turn);
            set_motors(driver_car->motors);
        } else if (command & cmd_turn_right) {
            if (driver_car->motors->turn >= STEERING_STRAIGHT) {
                if (driver_car->motors->turn != STEERING_ANGLE_MAX) {
                    driver_car->motors->turn += STEERING_STEP;
                    driver_car->motors->motor_right.value_speed -= SPEED_TURN_STEP;
                    if (driver_car->motors->motor_right.value_speed < 0) driver_car->motors->motor_right.value_speed = 0;
                    driver_car->motors->motor_right.new_value_speed = driver_car->motors->motor_right.value_speed;
                    ESP_LOGI(TAG, "Speed of right motor - %d", driver_car->motors->motor_right.value_speed);
                }
            } else if (driver_car->motors->turn < STEERING_STRAIGHT) {
                driver_car->motors->turn += STEERING_STEP;
                driver_car->motors->motor_left.value_speed += SPEED_TURN_STEP;
                if (driver_car->motors->motor_left.value_speed > driver_car->motors->duty_max_us) driver_car->motors->motor_left.value_speed = driver_car->motors->duty_max_us;
                driver_car->motors->motor_left.new_value_speed = driver_car->motors->motor_left.value_speed;
                ESP_LOGI(TAG, "Speed of left motor - %d", driver_car->motors->motor_left.value_speed);
            }
            set_steering(driver_car->motors->turn);
            set_motors(driver_car->motors);
        }



        if (driver_car->motors->motor_left.new_value_speed > driver_car->motors->motor_left.value_speed) {
            driver_car->motors->motor_left.value_speed += SPEEDUP_STEP;
            if (driver_car->motors->motor_left.value_speed > driver_car->motors->motor_left.new_value_speed) {
                driver_car->motors->motor_left.value_speed = driver_car->motors->motor_left.new_value_speed;
            }
            set_driver_pwm_us(&(driver_car->motors->motor_left.pwm_speed), driver_car->motors->motor_left.value_speed);
        }

        if (driver_car->motors->motor_left.new_value_speed < driver_car->motors->motor_left.value_speed) {
            driver_car->motors->motor_left.value_speed -= SPEEDUP_STEP;
            if (driver_car->motors->motor_left.value_speed < driver_car->motors->motor_left.new_value_speed) {
                driver_car->motors->motor_left.value_speed = driver_car->motors->motor_left.new_value_speed;
            }
            set_driver_pwm_us(&(driver_car->motors->motor_left.pwm_speed), driver_car->motors->motor_left.value_speed);
        }

        if (driver_car->motors->motor_right.new_value_speed > driver_car->motors->motor_right.value_speed) {
            driver_car->motors->motor_right.value_speed += SPEEDUP_STEP;
            if (driver_car->motors->motor_right.value_speed > driver_car->motors->motor_right.new_value_speed) {
                driver_car->motors->motor_right.value_speed = driver_car->motors->motor_right.new_value_speed;
            }
            set_driver_pwm_us(&(driver_car->motors->motor_right.pwm_speed), driver_car->motors->motor_right.value_speed);
        }

        if (driver_car->motors->motor_right.new_value_speed < driver_car->motors->motor_right.value_speed) {
            driver_car->motors->motor_right.value_speed -= SPEEDUP_STEP;
            if (driver_car->motors->motor_right.value_speed < driver_car->motors->motor_right.new_value_speed) {
                driver_car->motors->motor_right.value_speed = driver_car->motors->motor_right.new_value_speed;
            }
            set_driver_pwm_us(&(driver_car->motors->motor_right.pwm_speed), driver_car->motors->motor_right.value_speed);
        }

    }
}

/* ============================================================================================= */

static servomotor_t *create_steering_servo() {

    servomotor_t *servo = NULL;

    servo = malloc(sizeof(servomotor_t));

    if (servo == NULL) {
        ESP_LOGE(TAG, "Error allocation memory. (%s:%u)", __FILE__, __LINE__);
    } else {
        servo->current_position =           STEERING_STRAIGHT;
        servo->correction_center =          STEERING_CENTER;
        servo->delay =                      STEERING_DELAY;
        servo->degree_min =                 STEERING_ANGLE_MIN;
        servo->degree_max =                 STEERING_ANGLE_MAX;
        servo->duty_min_us =                SERVO_MIN_US;
        servo->duty_max_us =                SERVO_MAX_US;
        servo->mcpwm.gpio_num =             SERVO_PWM_GPIO;
        servo->mcpwm.unit =                 MCPWM_UNIT_0;
        servo->mcpwm.timer =                MCPWM_TIMER_0;
        servo->mcpwm.gen =                  MCPWM_GEN_A;
        servo->mcpwm.io_signal =            MCPWM0A;//(servo->mcpwm.timer) * 2 + servo->mcpwm.gen;
        servo->mcpwm.pwm_config.frequency = 50;     //frequency = 50Hz, i.e. for every servo motor time period should be 20ms
        servo->mcpwm.pwm_config.cmpr_a =    0;         //duty cycle of PWMxA = 0
        servo->mcpwm.pwm_config.cmpr_b =    0;         //duty cycle of PWMxb = 0
        servo->mcpwm.pwm_config.counter_mode = MCPWM_UP_COUNTER;
        servo->mcpwm.pwm_config.duty_mode = MCPWM_DUTY_MODE_0;

        mcpwm_gpio_init(servo->mcpwm.unit, servo->mcpwm.io_signal, servo->mcpwm.gpio_num);
        mcpwm_init(servo->mcpwm.unit, servo->mcpwm.timer, &(servo->mcpwm.pwm_config));    //Configure PWM0A & PWM0B with above settings

        vTaskDelay(500/portTICK_PERIOD_MS);
        set_driver_pwm_us(&(servo->mcpwm), angle_to_us(servo->current_position+servo->correction_center, servo->duty_min_us, servo->duty_max_us));
        vTaskDelay(1000/portTICK_PERIOD_MS);
        mcpwm_set_signal_low(servo->mcpwm.unit, servo->mcpwm.timer, servo->mcpwm.gen);

        ESP_LOGI(TAG, "Servo steering device created");
    }

    return servo;
}


static esp_err_t delete_steering_servo(servomotor_t *steering) {

    esp_err_t ret = ESP_FAIL;

    if (steering) {
        mcpwm_stop(steering->mcpwm.unit, steering->mcpwm.timer);
        free(steering);
        steering = NULL;
        ESP_LOGI(TAG, "Servo device steering deleted.");
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "No servo device was created, nothing deleted. (%s:%u)", __FILE__, __LINE__);
    }

    return ret;
}

static motors_t *create_motors() {

    motors_t *motors = NULL;

    motors = malloc(sizeof(motors_t));

    if (motors == NULL) {
        ESP_LOGE(TAG, "Error allocation memory. (%s:%u)", __FILE__, __LINE__);
        return NULL;
    }

    memset(motors, 0, sizeof(motors_t));
    motors->turn =                            STEERING_STRAIGHT;
    motors->status =                          motors_stop;
    motors->duty_min_us =                     VAL_SPEED_MIN;
    motors->duty_max_us =                     VAL_SPEED_MAX;

    motors->motor_left.gpio_motor_plus =      LEFT_MOTOR_GPIO_1;
    motors->motor_left.gpio_motor_minus =     LEFT_MOTOR_GPIO_2;
    motors->motor_left.value_speed =          VAL_SPEED_MIN;
    motors->motor_left.new_value_speed =      VAL_SPEED_MIN;

    motors->motor_right.gpio_motor_plus =     RIGHT_MOTOR_GPIO_1;
    motors->motor_right.gpio_motor_minus =    RIGHT_MOTOR_GPIO_2;
    motors->motor_right.value_speed =         VAL_SPEED_MIN;
    motors->motor_right.new_value_speed =     VAL_SPEED_MIN;

    motors->motor_left.pwm_speed.gpio_num =   LEFT_SPD_PWM_GPIO;
    motors->motor_left.pwm_speed.unit =       MCPWM_UNIT_0;
    motors->motor_left.pwm_speed.timer =      MCPWM_TIMER_1;
    motors->motor_left.pwm_speed.gen =        MCPWM_GEN_A;
    motors->motor_left.pwm_speed.io_signal =  MCPWM1A;
    motors->motor_left.pwm_speed.pwm_config.frequency = 200;    /* frequency = 200Hz       */
    motors->motor_left.pwm_speed.pwm_config.cmpr_a = 0;         /* duty cycle of PWMxA = 0 */
    motors->motor_left.pwm_speed.pwm_config.cmpr_b = 0;         /* duty cycle of PWMxb = 0 */
    motors->motor_left.pwm_speed.pwm_config.counter_mode = MCPWM_UP_COUNTER;
    motors->motor_left.pwm_speed.pwm_config.duty_mode = MCPWM_DUTY_MODE_0;

    mcpwm_gpio_init(motors->motor_left.pwm_speed.unit, motors->motor_left.pwm_speed.io_signal, motors->motor_left.pwm_speed.gpio_num);
    mcpwm_init(motors->motor_left.pwm_speed.unit, motors->motor_left.pwm_speed.timer, &(motors->motor_left.pwm_speed.pwm_config));

    motors->motor_right.pwm_speed.gpio_num =  RIGHT_SPD_PWM_GPIO;
    motors->motor_right.pwm_speed.unit =      MCPWM_UNIT_0;
    motors->motor_right.pwm_speed.timer =     MCPWM_TIMER_1;
    motors->motor_right.pwm_speed.gen =       MCPWM_GEN_B;
    motors->motor_right.pwm_speed.io_signal = MCPWM1B;
    motors->motor_right.pwm_speed.pwm_config.frequency = 200;   /* frequency = 1000Hz      */
    motors->motor_right.pwm_speed.pwm_config.cmpr_a = 0;        /* duty cycle of PWMxA = 0 */
    motors->motor_right.pwm_speed.pwm_config.cmpr_b = 0;        /* duty cycle of PWMxb = 0 */
    motors->motor_right.pwm_speed.pwm_config.counter_mode = MCPWM_UP_COUNTER;
    motors->motor_right.pwm_speed.pwm_config.duty_mode = MCPWM_DUTY_MODE_0;

    mcpwm_gpio_init(motors->motor_right.pwm_speed.unit, motors->motor_right.pwm_speed.io_signal, motors->motor_right.pwm_speed.gpio_num);
    mcpwm_init(motors->motor_right.pwm_speed.unit, motors->motor_right.pwm_speed.timer, &(motors->motor_right.pwm_speed.pwm_config));


    gpio_set_direction(motors->motor_left.gpio_motor_plus, GPIO_MODE_OUTPUT);
    gpio_set_direction(motors->motor_left.gpio_motor_minus, GPIO_MODE_OUTPUT);
    gpio_set_direction(motors->motor_right.gpio_motor_plus, GPIO_MODE_OUTPUT);
    gpio_set_direction(motors->motor_right.gpio_motor_minus, GPIO_MODE_OUTPUT);

    set_motors(motors);

    xTaskCreate(&speed_correction_task, "speed_correction_task", 2048, motors, 3, &(motors->handler_speed_correction_task));

    ESP_LOGI(TAG, "Motors device created");

    return motors;

}

static void delete_motors(motors_t *motors) {

    if (motors) {
        if (motors->handler_speed_correction_task) {
            vTaskDelete(motors->handler_speed_correction_task);
        }
        motors->motor_left.value_motor_plus = LOW;
        motors->motor_left.value_motor_minus = LOW;
        motors->motor_right.value_motor_plus = LOW;
        motors->motor_right.value_motor_minus = LOW;
        set_motors(motors);
        mcpwm_stop(motors->motor_left.pwm_speed.unit, motors->motor_left.pwm_speed.timer);
        mcpwm_stop(motors->motor_right.pwm_speed.unit, motors->motor_right.pwm_speed.timer);
        vTaskDelay(200/portTICK_PERIOD_MS);
        free(motors);
        motors = NULL;
        ESP_LOGI(TAG, "Motors device deleted.");
    } else {
        ESP_LOGE(TAG, "No motors device was created, nothing deleted.");
    }
}



/* ============================================================================================= */

static void straight_motors(motors_t *motors) {

    uint8_t delay = driver_car->steering->delay;

    if (motors->turn == STEERING_STRAIGHT) return;

    if (delay != 0) {
        driver_car->steering->delay = delay*2;
    }

    if (motors->turn < STEERING_STRAIGHT) {
        motors->motor_left.new_value_speed = motors->motor_right.value_speed;
    } else if (motors->turn > STEERING_STRAIGHT) {
        motors->motor_right.new_value_speed = motors->motor_left.value_speed;
    }

    motors->turn = STEERING_STRAIGHT;
    set_steering(motors->turn);

    for (uint8_t i = 0; driver_car->steering->current_position != motors->turn && i++ < 100; i++ ) {
        vTaskDelay(10/portTICK_PERIOD_MS);
    }

    if (delay != 0) {
        driver_car->steering->delay = delay;
    }
}

static void forward_motors(motors_t *motors) {
    int16_t speed = 0;
    bool speed_change = false;

    if (motors->status & motors_stop) {
        motors->motor_left.value_motor_plus = HIGH;
        motors->motor_left.value_motor_minus = LOW;
        motors->motor_right.value_motor_plus = HIGH;
        motors->motor_right.value_motor_minus = LOW;
        set_motors(motors);
        motors->status = motors_forward;
        speed_change = true;
    } else {
        if (motors->turn != STEERING_STRAIGHT && !(motors->status & motors_back)) {
            speed_change = false;
        } else {
            speed_change = true;
        }
    }

    straight_motors(motors);

    if (speed_change) {
        if (motors->status & motors_forward) {
            speed = cmd_speedup;
            if (xQueueSendToBack(driver_car->queue_driver, &speed, (TickType_t)10) != pdPASS) {
                ESP_LOGE(TAG, "Not put to queue motors cmd \"cmd_speedup\", command failed. (%s:%u)", __FILE__, __LINE__);
            }
        } else {
            speed = cmd_slowdown;
            if (xQueueSendToBack(driver_car->queue_driver, &speed, (TickType_t)10) != pdPASS) {
                ESP_LOGE(TAG, "Not put to queue motors cmd \"cmd_slowdown\", command failed. (%s:%u)", __FILE__, __LINE__);
            }
        }
    }

}

static void back_motors(motors_t *motors) {
    int16_t speed = 0;
    bool speed_change = false;

    if (motors->status & motors_stop) {
        motors->motor_left.value_motor_plus = LOW;
        motors->motor_left.value_motor_minus = HIGH;
        motors->motor_right.value_motor_plus = LOW;
        motors->motor_right.value_motor_minus = HIGH;
        set_motors(motors);
        motors->status = motors_back;
        speed_change = true;
    } else {
        if (motors->turn != STEERING_STRAIGHT && !(motors->status & motors_forward)) {
            speed_change = false;
        } else {
            speed_change = true;
        }
    }

    straight_motors(motors);

    if (speed_change) {
        if (!(motors->status & motors_forward)) {
            speed = cmd_speedup;
            if (xQueueSendToBack(driver_car->queue_driver, &speed, (TickType_t)50) != pdPASS) {
                ESP_LOGE(TAG, "Not put to queue motors cmd \"cmd_speedup\", command failed. (%s:%u)", __FILE__, __LINE__);
            }
        } else {
            speed = cmd_slowdown;
            if (xQueueSendToBack(driver_car->queue_driver, &speed, (TickType_t)50) != pdPASS) {
                ESP_LOGE(TAG, "Not put to queue motors cmd \"cmd_slowdown\", command failed. (%s:%u)", __FILE__, __LINE__);
            }
        }
    }

}

static void stop_motors(motors_t *motors) {
    motors->motor_left.value_motor_plus = LOW;
    motors->motor_left.value_motor_minus = LOW;
    motors->motor_right.value_motor_plus = LOW;
    motors->motor_right.value_motor_minus = LOW;
    motors->motor_left.new_value_speed = motors->motor_left.value_speed = VAL_SPEED_MIN;
    motors->motor_right.new_value_speed = motors->motor_right.value_speed = VAL_SPEED_MIN;

    set_motors(motors);
    motors->status = motors_stop;

}

static void set_speed_left_motor(int16_t speed) {
    driver_car->motors->motor_left.new_value_speed = speed;
}

static void set_speed_right_motor(int16_t speed) {
    driver_car->motors->motor_right.new_value_speed = speed;
}

static void set_speed_motors(int16_t speed) {

    set_speed_left_motor(speed);
    set_speed_right_motor(speed);
}


/* ============================================================================================= */
/*                                      Public zone                                              */
/* ============================================================================================= */

esp_err_t init_driver() {

    esp_err_t ret = ESP_FAIL;
    driver_t *driver = NULL;

    if (driver_car) {
        ESP_LOGE(TAG, "Driver already exist");
        return ret;
    }

    ESP_LOGI(TAG, "Initialize driver");

    driver = malloc(sizeof(driver_t));

    if (driver == NULL) {
        ESP_LOGE(TAG, "Error allocation memory. (%s:%u)", __FILE__, __LINE__);
        ESP_LOGE(TAG, "Could not init driver. (%s:%u)", __FILE__, __LINE__);
        return ret;
    }

    memset(driver, 0, sizeof(driver_t));

    driver->steering = create_steering_servo();

    if (driver->steering == NULL){
        ESP_LOGE(TAG, "Create servo steering failed. (%s:%u)", __FILE__, __LINE__);
        ESP_LOGE(TAG, "Could not init driver. (%s:%u)", __FILE__, __LINE__);
        free(driver);
        return ret;
    }

    driver->motors = create_motors();

    if (driver->motors == NULL) {
        ESP_LOGE(TAG, "Create motors device failed. (%s:%u)", __FILE__, __LINE__);
        ESP_LOGE(TAG, "Could not init driver. (%s:%u)", __FILE__, __LINE__);
        delete_steering_servo(driver_car->steering);
        free(driver);
        return ret;
    }

    driver->queue_driver = xQueueCreate(10, sizeof(int16_t));
    if (!driver->queue_driver) {
        ESP_LOGE(TAG, "Create driver queue failed. (%s:%u)", __FILE__, __LINE__);
        ESP_LOGE(TAG, "Could not init driver. (%s:%u)", __FILE__, __LINE__);
        delete_steering_servo(driver_car->steering);
        delete_motors(driver_car->motors);
        free(driver);
        return ret;
    }

    xTaskCreate(&driver_task, "driver_task", 4096, NULL, 5, &(driver->handler_driver_task));
    if (!driver->handler_driver_task) {
        ESP_LOGE(TAG, "Create driver task failed. (%s:%u)", __FILE__, __LINE__);
        ESP_LOGE(TAG, "Could not init driver. (%s:%u)", __FILE__, __LINE__);
        vQueueDelete(driver->queue_driver);
        delete_steering_servo(driver_car->steering);
        delete_motors(driver_car->motors);
        free(driver);
        return ret;
    }

    driver_car = driver;

    return ESP_OK;
}

void deinit_driver() {

    ESP_LOGI(TAG, "Deinitialize driver");

    if (driver_car) {
        if (driver_car->steering) {
            delete_steering_servo(driver_car->steering);
        }
        if (driver_car->motors) {
            delete_motors(driver_car->motors);
        }
        vTaskDelete(driver_car->handler_driver_task);
        vQueueDelete(driver_car->queue_driver);
        free(driver_car);
        driver_car = NULL;
    }

}


/* ============================================================================================= */

void turn_left_car() {

    int16_t left = cmd_turn_left;

    ESP_LOGI(TAG, "Turn left start");

    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }

    if (xQueueSendToBack(driver_car->queue_driver, &left, (TickType_t)50) != pdPASS) {
        ESP_LOGE(TAG, "Not put to queue driver cmd \"turn_left\", command failed. (%s:%u)", __FILE__, __LINE__);
    }
}

void turn_right_car() {

    int16_t right = cmd_turn_right;

    ESP_LOGI(TAG, "Turn right start");

    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }

    if (xQueueSendToBack(driver_car->queue_driver, &right, (TickType_t)50) != pdPASS) {
        ESP_LOGE(TAG, "Not put to queue driver cmd \"turn_right\", command failed. (%s:%u)", __FILE__, __LINE__);
    }
}

void turn_stop_car() {

    int16_t stop = cmd_turn_stop;

    ESP_LOGI(TAG, "Turn stop");

    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }

    if (xQueueSendToBack(driver_car->queue_driver, &stop, (TickType_t)50) != pdPASS) {
        ESP_LOGE(TAG, "Not put to queue driver cmd \"turn_stop\", command failed. (%s:%u)", __FILE__, __LINE__);
    }
}

void forward_start_car() {
    ESP_LOGI(TAG, "Forward start");
    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }
    forward_motors(driver_car->motors);
}

void forward_stop_car() {

    int16_t stop = cmd_speedstop;

    ESP_LOGI(TAG, "Forward stop");

    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }

    if (xQueueSendToBack(driver_car->queue_driver, &stop, (TickType_t)50) != pdPASS) {
        ESP_LOGE(TAG, "Not put to queue driver cmd \"speed_stop\", command failed. (%s:%u)", __FILE__, __LINE__);
    }
}

void back_start_car() {
    ESP_LOGI(TAG, "Back start");
    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }
    back_motors(driver_car->motors);
}

void back_stop_car() {

    int16_t stop = cmd_speedstop;

    ESP_LOGI(TAG, "Back stop");

    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }

    if (xQueueSendToBack(driver_car->queue_driver, &stop, (TickType_t)50) != pdPASS) {
        ESP_LOGE(TAG, "Not put to queue driver cmd \"speed_stop\", command failed. (%s:%u)", __FILE__, __LINE__);
    }
}

void stop_car() {
    ESP_LOGI(TAG, "Stop");
    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }
    stop_motors(driver_car->motors);
}

void set_speed_car(int16_t speed) {
    int16_t value_speed;

    if (!driver_car) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        return;
    }

    if (driver_car->motors->status & motors_stop ) return;

    if (speed < SPEED_MIN) speed = SPEED_MIN;
    if (speed > SPEED_MAX) speed = SPEED_MAX;

    value_speed =  map(speed, SPEED_MIN, SPEED_MAX, VAL_SPEED_MIN, VAL_SPEED_MAX);

    ESP_LOGI(TAG, "Setting speed in %d", value_speed);

    if (driver_car->motors->turn == STEERING_STRAIGHT) {
        set_speed_motors(value_speed);
    } else {
        if (driver_car->motors->turn < STEERING_STRAIGHT) {
            set_speed_right_motor(value_speed);
            value_speed -= SPEED_TURN_STEP*(STEERING_STRAIGHT - driver_car->motors->turn)/STEERING_STEP;
            set_speed_left_motor(value_speed);
        } else {
            set_speed_left_motor(value_speed);
            value_speed -= SPEED_TURN_STEP*(driver_car->motors->turn - STEERING_STRAIGHT)/STEERING_STEP;
            set_speed_right_motor(value_speed);
        }
    }
}

cJSON *get_status_car() {

    int16_t left_speed, right_speed, speed;

    const char *forward_key = "forward";
    const char *back_key =    "back";
    const char *turn_key =    "turn";
    const char *stop_key =    "stop";
    const char *speed_key =   "speed";
    const char *speed_l_key = "speed_left";     /* only for control */
    const char *speed_r_key = "speed_right";    /* only for control */

    char *err = NULL;

    cJSON *status_root = cJSON_CreateObject();

    if (status_root == NULL) {
        err = "Failed to create status_root json object";
        ESP_LOGE(TAG, "%s. (%s:%u)", err, __FILE__, __LINE__);
        return NULL;
    }

    if (driver_car == NULL) {
        ESP_LOGE(TAG, "No driver device created. (%s:%d)", __FILE__, __LINE__);
        cJSON_AddNumberToObject(status_root, forward_key, LOW);
        cJSON_AddNumberToObject(status_root, back_key, LOW);
        cJSON_AddNumberToObject(status_root, stop_key, LOW);
        cJSON_AddNumberToObject(status_root, speed_key, LOW);
        cJSON_AddNumberToObject(status_root, speed_l_key, LOW);
        cJSON_AddNumberToObject(status_root, speed_r_key, LOW);
        cJSON_AddNumberToObject(status_root, turn_key, STEERING_STRAIGHT);
    } else {
        left_speed =  map(driver_car->motors->motor_left.new_value_speed, VAL_SPEED_MIN, VAL_SPEED_MAX, SPEED_MIN, SPEED_MAX);
        right_speed =  map(driver_car->motors->motor_right.new_value_speed, VAL_SPEED_MIN, VAL_SPEED_MAX, SPEED_MIN, SPEED_MAX);

        if (left_speed > right_speed) speed = left_speed;
        else speed = right_speed;

        if (driver_car->motors->status & motors_forward) {
            cJSON_AddNumberToObject(status_root, forward_key, HIGH);
            cJSON_AddNumberToObject(status_root, back_key, LOW);
            cJSON_AddNumberToObject(status_root, stop_key, LOW);
        } else if (driver_car->motors->status & motors_back) {
            cJSON_AddNumberToObject(status_root, forward_key, LOW);
            cJSON_AddNumberToObject(status_root, back_key, HIGH);
            cJSON_AddNumberToObject(status_root, stop_key, LOW);
        } else {
            cJSON_AddNumberToObject(status_root, forward_key, LOW);
            cJSON_AddNumberToObject(status_root, back_key, LOW);
            cJSON_AddNumberToObject(status_root, stop_key, HIGH);
            speed = left_speed = right_speed = 0;
        }


        cJSON_AddNumberToObject(status_root, speed_key, speed);
        cJSON_AddNumberToObject(status_root, speed_l_key, left_speed);
        cJSON_AddNumberToObject(status_root, speed_r_key, right_speed);
        cJSON_AddNumberToObject(status_root, turn_key, driver_car->motors->turn);

    }


    return status_root;
}

