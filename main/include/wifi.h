#ifndef MAIN_INCLUDE_WIFI_H_
#define MAIN_INCLUDE_WIFI_H_

#include "config.h"

void setNullWiFiConfigDefault();
void startWiFiAP();
void startWiFiSTA();
void startWiFiSTA_AP();
char *getRssi();

#endif /* MAIN_INCLUDE_WIFI_H_ */
