#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A nonzero lock count keeps the firmware awake while async work is pending. */
void typewrt_sleep_lock(void);
void typewrt_sleep_unlock(void);
void typewrt_sleep_clear_ui_wakeup(void);
void typewrt_sleep_set_ui_wakeup_us(uint64_t delay_us);
void typewrt_sd_write_begin(void);
void typewrt_sd_write_end(void);
bool typewrt_usb_power_present(void);
bool typewrt_power_off(void);
