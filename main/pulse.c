#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/pcnt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


#define INPUT_LEFT      4                   // Pulse Input GPIO left motor
#define INPUT_RIGHT     5                   // Pulse Input GPIO right motor
#define UNIT_LEFT       PCNT_UNIT_0         // Unit left
#define UNIT_RIGHT      PCNT_UNIT_1         // Unit right
#define CHANNEL         PCNT_CHANNEL_0      // Channel left and right
#define PULSE_PER_TURN  11                  // number of pulses per rotation
#define COUNT_TIMEOUT   1000                // timeout without pulse in ms

typedef struct {
    uint64_t        time_previous;
    uint64_t        time_current;
    uint64_t        speed;
    pcnt_config_t   pcnt_config;
    QueueHandle_t   queue;
    TaskHandle_t    handler;
} speed_sensor_side_t;

typedef struct {
    speed_sensor_side_t *sensor_left;
    speed_sensor_side_t *sensor_right;
} speed_sensor_t;

static const char *TAG = "robot_car_pulse";
static speed_sensor_t *speed_sensor = NULL;

static void pulse_task(void *pvParameter) {

    speed_sensor_side_t *sensor = (speed_sensor_side_t*)pvParameter;

    int16_t unit;
    uint64_t start;


    start = esp_timer_get_time();
    while(1) {
        if (xQueueReceive(sensor->queue, &unit, 100/portTICK_PERIOD_MS) == pdTRUE) {
            sensor->speed  = sensor->time_current - sensor->time_previous;
            pcnt_counter_clear(sensor->pcnt_config.unit);
        } else {
            if (esp_timer_get_time() - start > COUNT_TIMEOUT*1000) {
                sensor->speed = 0;
                start = esp_timer_get_time();
            }
        }
    }
}

static void IRAM_ATTR speed_intr_handler_left(void *arg) {
    speed_sensor_side_t *sensor = (speed_sensor_side_t*)arg;
    sensor->time_previous = sensor->time_current;
    sensor->time_current = esp_timer_get_time();
    xQueueSendFromISR(sensor->queue, &(sensor->pcnt_config.unit), NULL);
}

static void IRAM_ATTR speed_intr_handler_right(void *arg) {
    speed_sensor_side_t *sensor = (speed_sensor_side_t*)arg;
    sensor->time_previous = sensor->time_current;
    sensor->time_current = esp_timer_get_time();
    xQueueSendFromISR(sensor->queue, &(sensor->pcnt_config.unit), NULL);
}


static speed_sensor_side_t *create_sensor_side(int pin, uint8_t pcnt_unit, uint8_t pcnt_channel) {
    esp_err_t ret = ESP_FAIL;
    speed_sensor_side_t *sensor;
    char task_name[16];

    if (speed_sensor) {
        if (speed_sensor->sensor_left && speed_sensor->sensor_right) {
            ESP_LOGE(TAG, "Two speed sensors have already been created. (%s:%u)", __FILE__, __LINE__);
            return NULL;
        }
    }

    sensor = malloc(sizeof(speed_sensor_side_t));

    if (sensor == NULL) {
        ESP_LOGE(TAG, "Error allocation memory. (%s:%u)", __FILE__, __LINE__);
        return NULL;
    }

    memset(sensor, 0, sizeof(speed_sensor_side_t));

    sensor->pcnt_config.pulse_gpio_num = pin;
    sensor->pcnt_config.ctrl_gpio_num = PCNT_PIN_NOT_USED;
    sensor->pcnt_config.channel = pcnt_channel;
    sensor->pcnt_config.unit = pcnt_unit;
    // What to do on the positive / negative edge of pulse input?
    sensor->pcnt_config.pos_mode = PCNT_COUNT_INC;          // Count up on the positive edge
    sensor->pcnt_config.neg_mode = PCNT_COUNT_DIS;          // Keep the counter value on the negative edge
    sensor->pcnt_config.lctrl_mode = PCNT_MODE_REVERSE;     // Reverse counting direction if low
    sensor->pcnt_config.hctrl_mode = PCNT_MODE_KEEP;        // Keep the primary counter mode if high
    sensor->pcnt_config.counter_l_lim = - PULSE_PER_TURN;

    /* Initialize PCNT unit */
    ret = pcnt_unit_config(&(sensor->pcnt_config));

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error set pcnt config. (%s:%u)", __FILE__, __LINE__);
        sensor->pcnt_config.pulse_gpio_num = PCNT_PIN_NOT_USED;
        sensor->pcnt_config.ctrl_gpio_num = PCNT_PIN_NOT_USED;
        pcnt_unit_config(&(sensor->pcnt_config));
        free(sensor);
        return NULL;
    }

    sensor->queue = xQueueCreate(10, sizeof(int16_t));

    if (!sensor->queue) {
        ESP_LOGE(TAG, "Create queue failed. (%s:%u)", __FILE__, __LINE__);
        sensor->pcnt_config.pulse_gpio_num = PCNT_PIN_NOT_USED;
        sensor->pcnt_config.ctrl_gpio_num = PCNT_PIN_NOT_USED;
        pcnt_unit_config(&(sensor->pcnt_config));
        free(sensor);
        return NULL;
    }

    sprintf(task_name, "pulse_task_%u", sensor->pcnt_config.unit);
    xTaskCreate(&pulse_task, task_name, 2048, sensor, 5, &(sensor->handler));
    if (!sensor->handler) {
        ESP_LOGE(TAG, "Create task \"%s\" failed. (%s:%u)", task_name, __FILE__, __LINE__);
        sensor->pcnt_config.pulse_gpio_num = PCNT_PIN_NOT_USED;
        sensor->pcnt_config.ctrl_gpio_num = PCNT_PIN_NOT_USED;
        pcnt_unit_config(&(sensor->pcnt_config));
        vQueueDelete(sensor->queue);
        free(sensor);
        return NULL;
    }

    /* Configure and enable the input filter */
    pcnt_set_filter_value(sensor->pcnt_config.unit, 100);
    pcnt_filter_enable(sensor->pcnt_config.unit);

//    pcnt_event_enable(sensor->pcnt_config.unit, PCNT_EVT_H_LIM);
    pcnt_event_enable(sensor->pcnt_config.unit, PCNT_EVT_L_LIM);


    /* Initialize PCNT's counter */
    pcnt_counter_pause(sensor->pcnt_config.unit);
    pcnt_counter_clear(sensor->pcnt_config.unit);


    return sensor;
}

static void delete_sensor_side(speed_sensor_side_t *sensor) {

    vTaskDelete(sensor->handler);
    vQueueDelete(sensor->queue);
    pcnt_isr_handler_remove(sensor->pcnt_config.unit);
    sensor->pcnt_config.pulse_gpio_num = PCNT_PIN_NOT_USED;
    sensor->pcnt_config.ctrl_gpio_num = PCNT_PIN_NOT_USED;
    pcnt_unit_config(&(sensor->pcnt_config));

    free(sensor);

}

/* ============================================================================================= */

esp_err_t init_pulse() {
    esp_err_t ret = ESP_FAIL;
    speed_sensor_side_t *sensor_side;
    speed_sensor_t *sensor = NULL;

    if (speed_sensor) {
        ESP_LOGE(TAG, "Speed sensor already exist");
        return ret;
    }

    ESP_LOGI(TAG, "Initialize pulse for speed sensor");

    sensor = malloc(sizeof(speed_sensor_t));

    if (sensor == NULL) {
        ESP_LOGE(TAG, "Error allocation memory. (%s:%u)", __FILE__, __LINE__);
        return ret;
    }

    memset(sensor, 0, sizeof(speed_sensor_t));

    sensor_side = create_sensor_side(INPUT_LEFT, UNIT_LEFT, CHANNEL);
    if (sensor_side == NULL) {
        ESP_LOGE(TAG, "Left speed sensor not created. (%s:%u)", __FILE__, __LINE__);
        free(sensor);
        return ret;
    }

    sensor->sensor_left = sensor_side;
    ESP_LOGI(TAG, "Speed sensor left side created");

    /* Install interrupt service and add isr callback handler */
    pcnt_isr_service_install(ESP_INTR_FLAG_LEVEL3);
//    pcnt_isr_service_install(0);
    pcnt_isr_handler_add(sensor_side->pcnt_config.unit, speed_intr_handler_left, sensor_side);
    /* Everything is set up, now go to counting */
    pcnt_counter_resume(sensor_side->pcnt_config.unit);

    sensor_side = create_sensor_side(INPUT_RIGHT, UNIT_RIGHT, CHANNEL);
    if (sensor_side == NULL) {
        ESP_LOGE(TAG, "Right speed sensor not created. (%s:%u)", __FILE__, __LINE__);
        delete_sensor_side(sensor->sensor_left);
        free(sensor);
        return ret;
    }

    sensor->sensor_right = sensor_side;
    ESP_LOGI(TAG, "Speed sensor right side created");

    pcnt_isr_handler_add(sensor_side->pcnt_config.unit, speed_intr_handler_right, sensor_side);
    /* Everything is set up, now go to counting */
    pcnt_counter_resume(sensor_side->pcnt_config.unit);

    vTaskDelay(500/portTICK_PERIOD_MS);

    speed_sensor = sensor;

    return ESP_OK;
}

void deinit_pulse() {
    if (speed_sensor) {
        ESP_LOGI(TAG, "Deinitialize speed sensor");
        if (speed_sensor->sensor_left) {
            delete_sensor_side(speed_sensor->sensor_left);
            ESP_LOGI(TAG, "Speed sensor left side deleted");
        }
        if (speed_sensor->sensor_right) {
            delete_sensor_side(speed_sensor->sensor_right);
            ESP_LOGI(TAG, "Speed sensor right side deleted");
        }
        pcnt_isr_service_uninstall();
        free(speed_sensor);
        speed_sensor = NULL;
    } else {
        ESP_LOGE(TAG, "Speed sensor was not initialized");
    }
}

void get_speed_time(uint64_t *speed_left, uint64_t *speed_right) {

    if (speed_sensor == NULL) {
       *speed_left  = 0;
       *speed_right = 0;
       return;
    }

    *speed_left  = speed_sensor->sensor_left->speed;
    *speed_right = speed_sensor->sensor_right->speed;

}


