idf_component_register(SRCS "vt_espnow.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_event
                                nvs_flash esp_timer voice_pipe convoy_proto)
