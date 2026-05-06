idf_component_register(SRCS "typewrt.c" "typewrt_display.c" "typewrt_keyboard_api.c" "nextvi_esp.c"
	               REQUIRES "esp_driver_gpio" 
		       PRIV_REQUIRES "esp_timer esp_driver_spi esp_driver_uart" 
                       INCLUDE_DIRS "../include")
