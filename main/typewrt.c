#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "keyboard_input.h"
#include "typewrt_display.h"
#include "typewrt_keyboard.h"

#define PIN_LDO2_EN 39
#define PIN_LEDN 17

typedef struct {
    enum Mode {
        HIDDEN,
        NORMAL,
        INSERT,
        REPLACE
    } mode;
    uint8_t x;
    uint8_t y;
} Cursor_t;

typedef struct {
    Cursor_t *cursor;
    QueueHandle_t keyboard;
} AppContext_t;

static const char *TAG = "typewrt";

static void process_printable_key(Virtual_Key vk, Cursor_t *cur)
{
    uint8_t fontchar = fontmap[vk - VKCHAROFFSET];
    typewrt_display_draw_glyph(fontchar, cur->x, cur->y);

    cur->x++;
    if (cur->x == TYPEWRT_DISPLAY_TEXT_COLUMNS) {
        cur->y++;
        cur->x = 0;
        if (cur->y == TYPEWRT_DISPLAY_TEXT_ROWS) {
            cur->y = 0;
        }
    }
}

static void vProcessKeyTask(void *pvParameters)
{
    uint8_t key = 0;
    AppContext_t *ctx = (AppContext_t *)pvParameters;

    while (1) {
        if (xQueueReceive(ctx->keyboard, (void *)&key, portMAX_DELAY) == pdTRUE) {
            bool keydown = (key & KEYDOWN_MASK);
            bool modifier = (key & MOD_MASK);

            if (modifier) {
                printf("Key is a modifier \n");
                if (keydown) {
                    KBD_MODS |= (key & KEY_MASK);
                } else {
                    KBD_MODS &= ~(key & KEY_MASK);
                }
            } else if (keydown) {
                Virtual_Key vk = keymap[(key & KEY_MASK)];
                if (vk < VKCHAROFFSET) {
                    printf("Key is a control key (non printable) \n");
                } else {
                    printf("Key is  \n");
                    process_printable_key(vk, ctx->cursor);
                }
            }
        } else {
            printf("Item Receive FALSE\n");
        }
        vTaskDelay(1);
    }
}

static void power_mng_init(void)
{
    gpio_config_t power_conf = {
        .pin_bit_mask = (1ULL << PIN_LDO2_EN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&power_conf);
    gpio_set_level(PIN_LDO2_EN, 1);
    gpio_hold_en(PIN_LDO2_EN);

    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_LEDN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(PIN_LEDN, 0);
    gpio_hold_en(PIN_LEDN);
}

void app_main(void)
{
    power_mng_init();
    esp_rom_delay_us(500);

    typewrt_display_init();
    typewrt_display_clear();

    static Cursor_t cursor;
    cursor.mode = NORMAL;

    static AppContext_t app_context;
    app_context.cursor = &cursor;
    app_context.keyboard = typewrt_keyboard_init();

    xTaskCreate(vProcessKeyTask, "keyboard", 2048, (void *)&app_context, 5, NULL);
    ESP_LOGI(TAG, "Keyboard started");
}
