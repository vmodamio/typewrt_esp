#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A nonzero lock count keeps the firmware awake while async work is pending. */
void typewrt_power_init(void);
bool typewrt_rtc_init(void);
bool typewrt_battery_init(void);
void typewrt_sleep_lock(void);
void typewrt_sleep_unlock(void);
void typewrt_sleep_clear_ui_wakeup(void);
void typewrt_sleep_set_ui_wakeup_us(uint64_t delay_us);
void typewrt_light_sleep_if_idle(void);
void typewrt_sd_write_begin(void);
void typewrt_sd_write_end(void);
size_t typewrt_heap_free_bytes(void);
size_t typewrt_heap_largest_free_block(void);
bool typewrt_usb_power_present(void);
bool typewrt_rtc_get_datetime(char *out, size_t out_len);
const char *typewrt_rtc_set_datetime(const char *datetime, char *out,
    size_t out_len);
bool typewrt_battery_get_status(char *out, size_t out_len);
void typewrt_reset_button_enable(bool enabled);
bool typewrt_power_off(void);
