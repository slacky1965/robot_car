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
#define TRIG_PIN        13
#define ECHO_PIN        12
#define BUFF_SIZE       0x8
#define ROUNDUP         58

#define ECHO_TIMEOUT    10000
#define TIME_MEASURE    50000

static char *TAG = "robot_car_usonic";

static uint64_t echo_resp_time[BUFF_SIZE] = {[0 ... BUFF_SIZE-1] = 0xffff};

/* return distance in ~ñm */
int16_t get_distance() {
    uint64_t value = 0;

    for(int i = 0; i < BUFF_SIZE; i++) {
        value += echo_resp_time[i];
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

    int level;
    uint8_t count = 0;

    while(1) {
        /* impulse of 10 us */
        gpio_set_level(TRIG_PIN, HIGH);
        ets_delay_us(10);
        gpio_set_level(TRIG_PIN, LOW);

        if (gpio_get_level(ECHO_PIN)) {
            vTaskDelay(50/portTICK_PERIOD_MS);
            continue;
        }

        /* read distance in us */
        for(int i = 0; i < ECHO_TIMEOUT; i++) {
            level = gpio_get_level(ECHO_PIN);
            if (level) {
                start = esp_timer_get_time();
                for(uint16_t i = 0; i < TIME_MEASURE && level; i++) {
                    level = gpio_get_level(ECHO_PIN);
                }
                finish = esp_timer_get_time();
                echo_resp_time[count] = finish - start;
                count = (count + 1)&(BUFF_SIZE-1);
                break;
            }
        }
        vTaskDelay(20/portTICK_PERIOD_MS);
    }
}

esp_err_t init_usonic() {

    esp_err_t ret;
    const int trig_gpio = TRIG_PIN;
    const int echo_gpio = ECHO_PIN;
    TaskHandle_t handler;

    ESP_LOGI(TAG, "Initialize ultrasonic");

    /* Configure gpio for trigger's output of HC-SR04 */
    ret = gpio_set_direction(trig_gpio, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gpio %d set failure. (%s:%u)", trig_gpio, __FILE__, __LINE__);
        return ret;
    }

    gpio_set_level(trig_gpio, LOW);

    /* Configure gpio for echo's input of HC-SR04 */
    ret = gpio_set_direction(echo_gpio, GPIO_MODE_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Gpio %d set failure. (%s:%u)", echo_gpio, __FILE__, __LINE__);
        return ret;
    }

    xTaskCreate(&usonic_task, "usonic_task", 2048, NULL, 0, &handler);
    if (!handler) {
        ESP_LOGE(TAG, "Create task failed. (%s:%u)", __FILE__, __LINE__);
        return ESP_FAIL;
    }

    vTaskDelay(200/portTICK_PERIOD_MS);

    return ESP_OK;
}
