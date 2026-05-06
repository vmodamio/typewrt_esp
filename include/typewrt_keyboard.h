#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    uint32_t scans;
    uint32_t press_events;
    uint32_t release_events;
    uint32_t sleep_entries;
    uint8_t last_event;
} typewrt_keyboard_debug_t;

QueueHandle_t typewrt_keyboard_init(void);
void typewrt_keyboard_debug_snapshot(typewrt_keyboard_debug_t *debug);
