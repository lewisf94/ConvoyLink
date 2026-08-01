idf_component_register(SRCS "sx1262.c" "vendor/ra01s.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "vendor"
                       REQUIRES driver convoy_pins)
