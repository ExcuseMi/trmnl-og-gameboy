idf_component_register(SRCS main.c gb_input_button.c save_flash.c
                       INCLUDE_DIRS ../boards/trmnl_og
                       REQUIRES gbcore epd esp_partition esp_timer esp_driver_gpio)
