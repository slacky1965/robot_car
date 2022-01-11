#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "usonic.h"

#define LOW             0
#define HIGH            1

/* defined pin for ultrasonic HC-SR04 */
#define TRIG_GPIO       GPIO_NUM_13
#define ECHO_GPIO       GPIO_NUM_12
#define TRIG_LOW_DELAY  4
#define TRIG_HIGH_DELAY 10
#define BUFF_SIZE       8
#define ECHO_TIMEOUT    10000
#define TIME_MEASURE    50000
#define ROUNDUP         58

static char *TAG = "robot_car_usonic";

typedef struct {
    int trig_gpio;
    int echo_gpio;
    uint32_t trig_low_delay;
    uint32_t trig_high_delay;
    uint64_t echo_resp_time[BUFF_SIZE];
    TaskHandle_t handler_usonic_task;
} usonic_t;

static usonic_t *usonic = NULL;

/* return distance in ~ñm */
int16_t get_distance() {
    uint64_t value = 0;

    if (usonic == NULL) {
        ESP_LOGE(TAG, "No ultrasonic device created. (%s:%d)", __FILE__, __LINE__);
        return -1;
    }

    for(int i = 0; i < BUFF_SIZE; i++) {
        value += usonic->echo_resp_time[i];
    }

    value = (value / BUFF_SIZE) & 0xffff;

    if (value == 0xffff) {
        ESP_LOGE(TAG, "Ultrasonic not connected. (%s:%u)", __FILE__, __LINE__);
        return value;
    }

    return  (value / ROUNDUP);
}

static void usonic_task(void *param) {

    uint64_t start, finish;

    usonic_t *sonic = (usonic_t*)param;

    int level;
    uint8_t count = 0;
    bool set_dist;

    while(1) {

        gpio_set_level(sonic->trig_gpio, LOW);
        ets_delay_us(sonic->trig_low_delay);
        /* impulse of 10 us */
        gpio_set_level(sonic->trig_gpio, HIGH);
        ets_delay_us(sonic->trig_high_delay);
        gpio_set_level(sonic->trig_gpio, LOW);

        if (gpio_get_level(sonic->echo_gpio)) {
            vTaskDelay(50/portTICK_PERIOD_MS);
            continue;
        }

        set_dist = false;
        /* read distance in us */
        for(int i = 0; i < ECHO_TIMEOUT; i++) {
            level = gpio_get_level(sonic->echo_gpio);
            if (level) {
                start = esp_timer_get_time();
                for(uint16_t i = 0; i < TIME_MEASURE && level; i++) {
                    level = gpio_get_level(sonic->echo_gpio);
                }
                finish = esp_timer_get_time();
                sonic->echo_resp_time[count++&(BUFF_SIZE-1)] = finish - start;
                set_dist = true;
                break;
            }
        }

        if (!set_dist) {
            sonic->echo_resp_time[count++&(BUFF_SIZE-1)] = -1;
        }
        vTaskDelay(20/portTICK_PERIOD_MS);
    }
}

static usonic_t *create_usonic() {

    usonic_t *sonic = NULL;

    sonic = malloc(sizeof(usonic_t));

    if (sonic == NULL) {
        ESP_LOGE(TAG, "Error allocation memory. (%s:%u)", __FILE__, __LINE__);
        return NULL;
    }

    sonic->trig_gpio = TRIG_GPIO;
    sonic->echo_gpio = ECHO_GPIO;

    sonic->trig_low_delay = TRIG_LOW_DELAY;
    sonic->trig_high_delay = TRIG_HIGH_DELAY;

    memset(&(sonic->echo_resp_time), -1, sizeof(uint64_t)*BUFF_SIZE);

    xTaskCreate(&usonic_task, "usonic_task", 2048, sonic, 0, &(sonic->handler_usonic_task));
    if (!sonic->handler_usonic_task) {
        ESP_LOGE(TAG, "Create ultrasonic task failed. (%s:%u)", __FILE__, __LINE__);
        free(sonic);
        return NULL;
    }

    ESP_LOGI(TAG, "Ultrasonic device created");

    return sonic;
}

static void delete_usonic(usonic_t *sonic) {

    if (sonic) {
        gpio_reset_pin(sonic->trig_gpio);
        gpio_reset_pin(sonic->echo_gpio);
        vTaskDelete(sonic->handler_usonic_task);
        free(sonic);
        ESP_LOGI(TAG, "Ultrasonic device deleted.");
    } else {
        ESP_LOGE(TAG, "No ultrasonic device was created, nothing deleted.");
    }

}

esp_err_t init_usonic() {

    esp_err_t ret = ESP_FAIL;
    usonic_t *sonic;

    ESP_LOGI(TAG, "Initialize ultrasonic");

    if (usonic) {
        ESP_LOGE(TAG, "Ultrasonic device already exist");
        return ret;
    }


    sonic = create_usonic();

    if (sonic == NULL) {
        ESP_LOGE(TAG, "Create ultrasonic device failed. (%s:%u)", __FILE__, __LINE__);
        ESP_LOGE(TAG, "Could not init ultrasonic. (%s:%u)", __FILE__, __LINE__);
        return ret;
    }

    gpio_reset_pin(sonic->trig_gpio);
    gpio_reset_pin(sonic->echo_gpio);

    /* Configure gpio for trigger's output of HC-SR04 */
    ret = gpio_set_direction(sonic->trig_gpio, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Trigger GPIO_NUM%d set failure. (%s:%u)", sonic->trig_gpio, __FILE__, __LINE__);
        ESP_LOGE(TAG, "Could not init ultrasonic. (%s:%u)", __FILE__, __LINE__);
        delete_usonic(sonic);
        return ret;
    }

    gpio_set_level(sonic->trig_gpio, LOW);

    /* Configure gpio for echo's input of HC-SR04 */
    ret = gpio_set_direction(sonic->echo_gpio, GPIO_MODE_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Echo GPIO_NUM%d set failure. (%s:%u)", sonic->echo_gpio, __FILE__, __LINE__);
        ESP_LOGE(TAG, "Could not init ultrasonic. (%s:%u)", __FILE__, __LINE__);
        delete_usonic(sonic);
        return ret;
    }

    vTaskDelay(200/portTICK_PERIOD_MS);

    usonic = sonic;

    return ESP_OK;
}

void deinit_usonic() {

    if (usonic) {
        ESP_LOGI(TAG, "Deinitialize ultrasonic");
        delete_usonic(usonic);
        usonic = NULL;
    } else {
        ESP_LOGE(TAG, "Ultrasonic was not initialized");
    }
}
