idf_component_register(SRCS "typewrt.c"
	               REQUIRES "esp_driver_gpio" 
		       PRIV_REQUIRES "esp_timer esp_driver_spi esp_driver_uart" 
                       INCLUDE_DIRS "../include")
