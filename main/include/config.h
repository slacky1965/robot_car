#ifndef MAIN_INCLUDE_CONFIG_H_
#define MAIN_INCLUDE_CONFIG_H_

#define LOW  0
#define HIGH 1
#define MOUNT_POINT_SPIFFS  "/spiffs"
#define DELIM               "/"
#define DELIM_CHR           '/'

/*--------------------------WiFi Zone-------------------------------------------*/
#define MY_STA_SSID         "MyNet"
#define MY_STA_PASSWORD     "12345678"
#define MY_AP_SSID          "Robot_car"
#define MY_AP_PASSWORD      "12345678"
#define STA_MODE            true

/*--------------------------Webserver Zone--------------------------------------*/
#define HTML_PATH MOUNT_POINT_SPIFFS DELIM "html"
#define MODULE_NAME         "Robot Car"

/*--------------------------Pulse counter Zone----------------------------------*/
#define INPUT_LEFT          4                   // Pulse Input GPIO left motor
#define INPUT_RIGHT         5                   // Pulse Input GPIO right motor
#define UNIT_LEFT           PCNT_UNIT_0         // Unit left
#define UNIT_RIGHT          PCNT_UNIT_1         // Unit right
#define CHANNEL             PCNT_CHANNEL_0      // Channel left and right
#define PULSE_PER_TURN      11                  // number of pulses per rotation
#define COUNT_TIMEOUT       1000                // timeout without pulse in ms

/*--------------------------Ultrasonic HC-SR04 zone-----------------------------*/
#define TRIG_GPIO           GPIO_NUM_13
#define ECHO_GPIO           GPIO_NUM_12

/*--------------------------Driver (motors and servo steering) Zone-------------*/
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


#endif /* MAIN_INCLUDE_CONFIG_H_ */
