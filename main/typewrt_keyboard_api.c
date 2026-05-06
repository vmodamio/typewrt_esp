#include "typewrt_keyboard.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "soc/gpio_struct.h"

#include "keyboard_input.h"

#define IOSIZE 8

#define PIN_KBD_IO7 1
#define PIN_KBD_IO6 7
#define PIN_KBD_IO5 10
#define PIN_KBD_IO4 11
#define PIN_KBD_IO3 5
#define PIN_KBD_IO2 6
#define PIN_KBD_IO1 12
#define PIN_KBD_IO0 14
#define PIN_KBD_OE 16
#define PIN_KBD_LE 15

#define SCANTIMEOUT 500
#define SCANPERIOD 1500
#define KBD_EVENT_QUEUE_LENGTH 32
#define KBD_EVENT_SIZE sizeof(uint8_t)

static StaticQueue_t s_kbd_static_queue;
static uint8_t s_kbd_queue_storage[KBD_EVENT_QUEUE_LENGTH * KBD_EVENT_SIZE];
static QueueHandle_t s_keyboard_queue;

static const volatile int KBD_IO[IOSIZE] = {
    PIN_KBD_IO0, PIN_KBD_IO1, PIN_KBD_IO2, PIN_KBD_IO3,
    PIN_KBD_IO4, PIN_KBD_IO5, PIN_KBD_IO6, PIN_KBD_IO7,
};
static volatile uint32_t KBD_IO_MASK = ((1UL << PIN_KBD_IO0) | (1UL << PIN_KBD_IO1) | (1UL << PIN_KBD_IO2) |
                                        (1UL << PIN_KBD_IO3) | (1UL << PIN_KBD_IO4) | (1UL << PIN_KBD_IO5) |
                                        (1UL << PIN_KBD_IO6) | (1UL << PIN_KBD_IO7));
static const volatile int KBD_OE = PIN_KBD_OE;
static const volatile int KBD_LE = PIN_KBD_LE;
static esp_timer_handle_t KBD_SCAN_TIMER;
static volatile uint8_t KBD_COLS[IOSIZE];
static volatile uint8_t KBD_COLFLAGS[IOSIZE];
static volatile uint8_t KBD_BUFFER[(IOSIZE * IOSIZE)];
static volatile int KBD_SCANCOUNT;
static volatile bool KBD_NOKEY = true;

static inline IRAM_ATTR void cycle(uint32_t cycles)
{
    for (int i = 0; i < cycles; i++) {
        __asm__ __volatile__("nop");
    }
}

static IRAM_ATTR void kbd_scan(void *arg)
{
    int col = -1;
    int row = -1;
    uint8_t key_event = 0;

    if (KBD_SCANCOUNT) {
        KBD_NOKEY = true;
        for (row = 0; row < IOSIZE; row++) {
            GPIO.out_w1ts = (1UL << KBD_OE);
            cycle(16);
            GPIO.out_w1tc = KBD_IO_MASK;
            GPIO.enable_w1ts = KBD_IO_MASK;
            cycle(32);
            GPIO.out_w1ts = KBD_IO_MASK;
            GPIO.out_w1tc = (1UL << KBD_IO[row]);
            cycle(16);
            GPIO.out_w1ts = (1UL << KBD_LE);
            cycle(32);
            GPIO.out_w1tc = (1UL << KBD_LE);
            cycle(8);
            GPIO.out_w1ts = KBD_IO_MASK;
            cycle(16);
            GPIO.enable_w1tc = KBD_IO_MASK;
            cycle(32);
            GPIO.out_w1tc = (1UL << KBD_OE);
            cycle(16);

            uint32_t cols_read = GPIO.in;
            uint32_t cols_in = 0;
            cols_read &= KBD_IO_MASK;
            for (int k = 0; k < IOSIZE; k++) {
                cols_in |= (((cols_read >> KBD_IO[k]) & 1) << k);
            }

            KBD_COLFLAGS[row] |= (cols_in ^ KBD_COLS[row]);
            uint8_t scan = KBD_COLFLAGS[row];
            while (scan) {
                col = __builtin_ffs(scan) - 1;
                KBD_BUFFER[(IOSIZE * row + col)] =
                    (KBD_BUFFER[(IOSIZE * row + col)] << 1) | ((cols_in >> col) & 1);

                if ((KBD_BUFFER[(IOSIZE * row + col)] == 0x00) && (KBD_COLS[row] & (1 << col))) {
                    KBD_COLS[row] &= ~(1 << col);
                    KBD_SCANCOUNT = SCANTIMEOUT;
                    key_event = KBDMAP[(IOSIZE * row + col)] | KEYDOWN_MASK;
                    xQueueSend(s_keyboard_queue, &key_event, portMAX_DELAY);
                    KBD_COLFLAGS[row] &= (~(1 << col) & ((1 << IOSIZE) - 1));
                }

                if ((KBD_BUFFER[(IOSIZE * row + col)] == 0xFF) && ~(KBD_COLS[row] & (1 << col))) {
                    KBD_COLS[row] |= (1 << col);
                    KBD_SCANCOUNT = SCANTIMEOUT;
                    key_event = KBDMAP[(IOSIZE * row + col)];
                    xQueueSend(s_keyboard_queue, &key_event, portMAX_DELAY);
                    KBD_COLFLAGS[row] &= (~(1 << col) & ((1 << IOSIZE) - 1));
                }

                scan &= (scan - 1);
            }

            if ((~KBD_COLS[row]) & ((1UL << IOSIZE) - 1)) {
                KBD_NOKEY = false;
            }
        }

        if (KBD_NOKEY) {
            KBD_SCANCOUNT--;
        }
    } else {
        ESP_ERROR_CHECK(esp_timer_stop(KBD_SCAN_TIMER));
        GPIO.out_w1ts = (1UL << KBD_OE);
        cycle(16);
        GPIO.enable_w1ts = KBD_IO_MASK;
        cycle(32);
        GPIO.out_w1tc = KBD_IO_MASK;
        GPIO.out_w1ts = (1UL << KBD_LE);
        cycle(4);
        GPIO.out_w1tc = (1UL << KBD_LE);
        cycle(32);
        cycle(4);
        GPIO.enable_w1tc = KBD_IO_MASK;
        cycle(32);
        GPIO.out_w1tc = (1UL << KBD_OE);
        cycle(16);

        ESP_ERROR_CHECK(esp_light_sleep_start());

        KBD_SCANCOUNT = SCANTIMEOUT;
        ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));
    }
}

QueueHandle_t typewrt_keyboard_init(void)
{
    s_keyboard_queue = xQueueCreateStatic(
        KBD_EVENT_QUEUE_LENGTH,
        KBD_EVENT_SIZE,
        &s_kbd_queue_storage[0],
        &s_kbd_static_queue);

    const esp_timer_create_args_t scan_timer_args = {
        .callback = &kbd_scan,
        .name = "scaner",
    };
    esp_timer_create(&scan_timer_args, &KBD_SCAN_TIMER);

    for (int k = 0; k < IOSIZE; k++) {
        gpio_reset_pin(KBD_IO[k]);
        KBD_COLS[k] = 0xFF;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = KBD_IO_MASK,
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    gpio_reset_pin(KBD_LE);
    gpio_reset_pin(KBD_OE);

    gpio_config_t ol_config = {
        .pin_bit_mask = ((1ULL << KBD_OE) | (1ULL << KBD_LE)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ol_config);

    for (int i = 0; i < IOSIZE; i++) {
        gpio_wakeup_enable(KBD_IO[i], GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();

    KBD_SCANCOUNT = SCANTIMEOUT;
    ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));

    return s_keyboard_queue;
}
