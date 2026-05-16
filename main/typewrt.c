/*
 * SPDX-FileCopyrightText: 2020-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "typewrt_board.h"
#include "typewrt_ble.h"
#include "typewrt_display.h"
#include "typewrt_keyboard.h"
#include "typewrt_power.h"
#include "typewrt_sdcard.h"

static const char *TAG = "typewrt";

void nextvi_main(int argc, char *argv[]);

static void vNextviTask(void *pvParameters)
{
    char *argv[] = {"vi"};

    (void)pvParameters;
    if (typewrt_sdcard_is_mounted()) {
        setenv("PWD", TYPEWRT_SD_MOUNT_POINT, 1);
    }
    nextvi_main(1, argv);
    vTaskDelete(NULL);
}

void app_main(void)
{
    typewrt_power_init();

    esp_rom_delay_us(500);

    typewrt_rtc_init();
    typewrt_battery_init();
    typewrt_sdcard_spi_pins_prepare();
    typewrt_display_init();
    typewrt_display_clear();
    typewrt_sdcard_init();

    if (!typewrt_keyboard_init()) {
        ESP_LOGE(TAG, "Keyboard queue allocation failed");
        return;
    }
    xTaskCreate(vNextviTask, "nextvi", 16384, NULL, 5, NULL);

    typewrt_keyboard_start();
    typewrt_display_splash_clock_start();
    ESP_LOGI(TAG, "Keyboard started");
}
