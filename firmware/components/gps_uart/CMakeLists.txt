idf_component_register(SRCS "gps_uart.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver nmea convoy_pins esp_timer)
