#pragma once

#include <stdbool.h>

void typewrt_sdcard_spi_pins_prepare(void);
bool typewrt_sdcard_init(void);
bool typewrt_sdcard_is_mounted(void);
bool typewrt_sdcard_unmount_for_poweroff(void);
