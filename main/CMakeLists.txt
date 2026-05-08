idf_component_register(SRCS "typewrt.c" "../nextvi/vi.c"
	               REQUIRES "esp_driver_gpio" 
		       PRIV_REQUIRES "esp_timer esp_driver_spi esp_driver_uart" 
                       INCLUDE_DIRS "../include" "../nextvi")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    NEXTVI_EMBEDDED
    NEXTVI_NOTERM
    NEXTVI_DISPLAY_COLS=40
    NEXTVI_DISPLAY_ROWS=14
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=implicit-fallthrough
)
