#pragma once

#include <stdbool.h>
#include "esp_err.h"

bool typewrt_keyboard_init(void);
void typewrt_keyboard_start(void);
bool typewrt_keyboard_idle(void);
void typewrt_keyboard_prepare_wakeup_rows(void);
esp_err_t typewrt_keyboard_stop_scanning(void);
esp_err_t typewrt_keyboard_start_scanning(void);
void typewrt_keyboard_disable_wakeup(void);
void typewrt_keyboard_power_pins_high_z(void);
