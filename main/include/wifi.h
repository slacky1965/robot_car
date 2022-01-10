#ifndef MAIN_INCLUDE_WIFI_H_
#define MAIN_INCLUDE_WIFI_H_

#define MY_STA_SSID     "NyNet"
#define MY_STA_PASSWORD "111111111"
#define MY_AP_SSID     "Robot_car"
#define MY_AP_PASSWORD "12345678"

void setNullWiFiConfigDefault();
void startWiFiAP();
void startWiFiSTA();
void startWiFiSTA_AP();
char *getRssi();

#endif /* MAIN_INCLUDE_WIFI_H_ */
