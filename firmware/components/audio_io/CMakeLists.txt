idf_component_register(SRCS "audio_io.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2s convoy_pins convoy_proto)
