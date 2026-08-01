idf_component_register(SRCS "main.c"
                              "app_state.c"
                              "app_queues.c"
                              "radio_task.c"
                              "voice_task.c"
                              "gps_task.c"
                              "ui_task.c"
                              "ctrl_task.c"
                       INCLUDE_DIRS "."
                       REQUIRES unit_cfg sx1262 audio_io gps_uart ili9341_disp
                                radar_render neighbor_table convoy_geo
                                convoy_proto console nvs_flash esp_timer driver)
