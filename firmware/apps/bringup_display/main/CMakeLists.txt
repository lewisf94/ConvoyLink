idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES ili9341_disp radar_render console esp_timer)
