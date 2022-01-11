#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "driver/mcpwm.h"
#include "soc/mcpwm_periph.h"



#include "driver.h"
#include "utils.h"
#include "pulse.h"
#include "usonic.h"
#include "http.h"
#include "wifi.h"

static char *TAG = "robot_car_main";

void app_main(void) {
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Startup...");
    ESP_LOGI(TAG, "Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    setNullWiFiConfigDefault();

    webserver_init(HTML_PATH);
//    videoserver_init();

    startWiFiSTA();
//    startWiFiAP();

    init_spiffs();
    init_usonic();
    init_driver();
//    init_driver();
    init_pulse();
//    init_pulse();
    vTaskDelay(5000/portTICK_PERIOD_MS);
//    deinit_pulse();
//    deinit_driver();
    int16_t distance = -1;

    for (;;) {

//        distance = get_distance();

        if (distance != -1) {
            printf("distance - %d cm\n", distance);
        }

//        printf("Free memory: %d bytes\n", esp_get_free_heap_size());
        vTaskDelay(5000/portTICK_PERIOD_MS);
    }

}
