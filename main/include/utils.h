#ifndef MAIN_INCLUDE_UTILS_H_
#define MAIN_INCLUDE_UTILS_H_

#include "config.h"

#define MAX_BUFF_RW         2048

bool get_status_spiffs();
size_t get_fs_free_space();
void init_spiffs();

#endif /* MAIN_INCLUDE_UTILS_H_ */
