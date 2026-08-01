idf_component_register(SRCS "ili9341_disp.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd driver convoy_pins radar_render)
