#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "nextvi_esp.h"
#include "typewrt_display.h"
#include "typewrt_keyboard.h"

#define PIN_LDO2_EN 39
#define PIN_LEDN 17
#define PIN_RST_EN 18

static const char *TAG = "typewrt";

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
        .pin_bit_mask = (1ULL << PIN_LEDN ) | (1ULL << PIN_RST_EN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(PIN_LEDN, 0);
    gpio_hold_en(PIN_LEDN);
    gpio_set_level(PIN_RST_EN, 0);
    gpio_hold_en(PIN_RST_EN);
}

void app_main(void)
{
    QueueHandle_t keyboard;

    power_mng_init();
    esp_rom_delay_us(500);

    typewrt_display_init();
    typewrt_display_clear();

    keyboard = typewrt_keyboard_init();
    ESP_LOGI(TAG, "Keyboard started, launching nextvi");
    nextvi_esp_run(keyboard);
}
