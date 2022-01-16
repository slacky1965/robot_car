#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "wifi.h"

#define WIFI_CONNECTED_BIT 		BIT0
#define WIFI_FAIL_BIT      		BIT1
#define ESP_MAXIMUM_RETRY  		5
#define DEFAULT_SCAN_LIST_SIZE 	20

static const char *TAG = "robot_car_wifi";

static bool staApModeNow;
static bool apModeNow;
static bool staModeNow;


static int s_retry_num = 0;

static esp_netif_t *esp_netif;
esp_netif_ip_info_t ip_info;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* Received signal strength indicator in dBm */
char* getRssi() {
    static char buff[16];

    wifi_ap_record_t info;

    esp_wifi_sta_get_ap_info(&info);
    sprintf(buff, "WiFi: %d dBm", info.rssi);

    return buff;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data) {

    static bool staoff = false;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        staoff = false;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT
            && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry %d to connect to the station \"%s\"", s_retry_num, MY_STA_SSID);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        if (!staoff) {
            staoff = true;
        }
        ESP_LOGI(TAG, "Connect to the station \"%s\" fail", MY_STA_SSID);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;
        printf("Station mode. Please go to http://" IPSTR "\n", IP2STR(&event->ip_info.ip));
        ip_info = event->ip_info;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        staoff = false;
    }

}

void wifi_check_task(void *pvParameter) {

    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count;

    for (;;) {
        if (s_retry_num >= ESP_MAXIMUM_RETRY) {
            ap_count = 0;
            memset(ap_info, 0, sizeof(ap_info));

            if (!staApModeNow) {
                startWiFiSTA_AP();
            }

            esp_wifi_disconnect();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            esp_wifi_scan_start(NULL, true);
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
            for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count);
                    i++) {
                if (strcmp((const char*) (ap_info[i].ssid), MY_STA_SSID) == 0) {
                    ESP_LOGI(TAG, "WiFi station \"%s\" is on now", ap_info[i].ssid);
                    s_retry_num = 0;
                    startWiFiSTA();
                }
            }
            esp_wifi_scan_stop();
            esp_wifi_connect();
        }
        vTaskDelay(30000 / portTICK_PERIOD_MS);
    }
}

void wifi_init() {

    s_wifi_event_group = xEventGroupCreate();
    xTaskCreate(&wifi_check_task, "wifi_check_task", 4096, NULL, 5, NULL);

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_netif_init());

    esp_netif_config_t cfg_netif = ESP_NETIF_DEFAULT_ETH();
    esp_netif = esp_netif_new(&cfg_netif);
}

void setNullWiFiConfigDefault() {

    wifi_init();

    wifi_config_t ap_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_AP, &ap_config));

    wifi_config_t sta_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &sta_config));

    ap_config.ap.ssid[0] = 0;
//	ap_config.ap.password[0] = 0;

    sta_config.ap.ssid[0] = 0;
//	sta_config.ap.password[0] = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

}

static void print_start_ap_msg() {
    printf("WiFi network Name: %s, Password: %s\n", MY_AP_SSID, MY_AP_PASSWORD);
    printf("Please go to http://" IPSTR "\n", IP2STR(&ip_info.ip));
}

void startWiFiSTA_AP() {

    wifi_config_t ap_config;
    wifi_config_t sta_config;

    staApModeNow = true;
    apModeNow = false;
    staModeNow = false;

    printf("Start WiFi AP+STA Mode\n");

    ESP_ERROR_CHECK(esp_wifi_stop());

//	ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_AP, &ap_config));
//	ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &sta_config));
    memset(&ap_config, 0, sizeof(wifi_config_t));
    memset(&sta_config, 0, sizeof(wifi_config_t));

    strcpy((char*) &(ap_config.ap.ssid), MY_AP_SSID);
    strcpy((char*) &(ap_config.ap.password), MY_AP_PASSWORD);

    ap_config.ap.ssid_len = 0;
//	ap_config.ap.ssid_len = strlen(MY_AP_SSID)+1;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 5;

    strcpy((char*) &(sta_config.sta.ssid), MY_STA_SSID);
    strcpy((char*) &(sta_config.sta.password), MY_STA_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_dhcp_status_t status;
    ESP_ERROR_CHECK(esp_netif_dhcpc_get_status(esp_netif, &status));

    if (status != TCPIP_ADAPTER_DHCP_STOPPED)
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(esp_netif));

    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif, &ip_info));

    print_start_ap_msg();
}

void startWiFiAP() {

    wifi_config_t ap_config;

    printf("Start WiFi AP Mode\n");

    ESP_ERROR_CHECK(esp_wifi_stop());

//	ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_AP, &ap_config));
    memset(&ap_config, 0, sizeof(wifi_config_t));

    strcpy((char*) &(ap_config.ap.ssid), MY_AP_SSID);
    strcpy((char*) &(ap_config.ap.password), MY_AP_PASSWORD);
    ap_config.ap.ssid_len = 0;
//	ap_config.ap.ssid_len = strlen(MY_AP_SSID)+1;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 5;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_dhcp_status_t status;
    ESP_ERROR_CHECK(esp_netif_dhcpc_get_status(esp_netif, &status));

    if (status != TCPIP_ADAPTER_DHCP_STOPPED)
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(esp_netif));

    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif, &ip_info));

    print_start_ap_msg();

    apModeNow = true;
    staModeNow = false;
    staApModeNow = false;

}

void startWiFiSTA() {

    wifi_config_t sta_config;

    printf("Start WiFi STA Mode\n");
    printf("Connecting to: %s\n", MY_STA_SSID);

    ESP_ERROR_CHECK(esp_wifi_stop());
//	ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &sta_config));
    memset(&sta_config, 0, sizeof(wifi_config_t));

    strcpy((char*) &(sta_config.sta.ssid), MY_STA_SSID);
    strcpy((char*) &(sta_config.sta.password), MY_STA_PASSWORD);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    apModeNow = false;
    staApModeNow = false;
    staModeNow = true;

}

