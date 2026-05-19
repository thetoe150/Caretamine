idf_component_register(SRCS "KY037_sound_sensor.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_driver_dac esp_driver_gpio esp_adc esp_driver_rmt)
