#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

QueueHandle_t typewrt_keyboard_init(void);
