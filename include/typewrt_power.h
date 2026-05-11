#pragma once

/* A nonzero lock count keeps the firmware awake while async work is pending. */
void typewrt_sleep_lock(void);
void typewrt_sleep_unlock(void);
void typewrt_power_off(void);
