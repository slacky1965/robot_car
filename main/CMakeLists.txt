idf_component_register(SRCS  "main.c"
                             "utils.c"
                             "driver.c"
                             "pulse.c"
                             "usonic.c"
                             "http.c"
                             "wifi.c"
                INCLUDE_DIRS "include")

spiffs_create_partition_image(storage ../storage FLASH_IN_PROJECT)
                