idf_component_register(
    SRCS "ad_keys.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc nvs_flash freertos esp_timer log
)
