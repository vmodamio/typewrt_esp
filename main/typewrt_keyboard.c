#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "soc/gpio_struct.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"

#include "typewrt_board.h"
#include "typewrt_keyboard.h"
#include "typewrt_keymap.h"
#include "typewrt_power.h"
#undef MIN
#undef MAX
#include "vi.h"


#define KBD_EVENT_QUEUE_LENGTH 32
#define KBD_EVENT_SIZE sizeof(uint8_t)

static StaticQueue_t kbd_StaticQueue;
static uint8_t kbd_QueueStorage[KBD_EVENT_QUEUE_LENGTH * KBD_EVENT_SIZE];
static QueueHandle_t keyboard;

bool typewrt_keyboard_init(void)
{
    if (keyboard) {
        return true;
    }
    keyboard = xQueueCreateStatic(KBD_EVENT_QUEUE_LENGTH,
        KBD_EVENT_SIZE, kbd_QueueStorage, &kbd_StaticQueue);
    return keyboard != NULL;
}

static unsigned char nextvi_keyboard_translate_event(unsigned char event)
{
    unsigned char press = event & KEYDOWN_MASK;
    unsigned char code = event & KEY_MASK;

    if ((event & MOD_MASK) != 0) {
        unsigned char nextvi_mod = 0;

        if (code & (1 << 0)) nextvi_mod |= NEXTVI_MOD_SHIFT;
        if (code & (1 << 1)) nextvi_mod |= NEXTVI_MOD_CTRL;
        if (code & (1 << 2)) nextvi_mod |= NEXTVI_MOD_WIN;
        if (code & (1 << 3)) nextvi_mod |= NEXTVI_MOD_ALT;
        if (code & (1 << 4)) nextvi_mod |= NEXTVI_MOD_CAPS;
        if (code & (1 << 5)) nextvi_mod |= NEXTVI_KEY_SIDE;

        return press | NEXTVI_KEY_MODIFIER | nextvi_mod;
    }

    if (code == 29) {
        return press | NEXTVI_KEY_MODIFIER | NEXTVI_MOD_CAPS;
    }

    return event;
}

static int nextvi_keyboard_read_wait(unsigned char *event, TickType_t wait_ticks,
    bool return_on_idle)
{
    if (!keyboard) {
        return 0;
    }
    while (xQueueReceive(keyboard, event, wait_ticks) != pdTRUE) {
        typewrt_light_sleep_if_idle();
        if (return_on_idle) {
            return 0;
        }
    }
    *event = nextvi_keyboard_translate_event(*event);
    return 1;
}

int nextvi_keyboard_read(unsigned char *event)
{
    return nextvi_keyboard_read_wait(event,
        pdMS_TO_TICKS(TYPEWRT_LIGHT_SLEEP_IDLE_MS), false);
}

int nextvi_keyboard_read_timeout(unsigned char *event, int timeout_ms)
{
    if (timeout_ms < 0) {
        return nextvi_keyboard_read(event);
    }
    return nextvi_keyboard_read_wait(event, pdMS_TO_TICKS(timeout_ms), true);
}

#define IOSIZE 8

#define PIN_KBD_IO7  TYPEWRT_PIN_KBD_IO7
#define PIN_KBD_IO6  TYPEWRT_PIN_KBD_IO6
#define PIN_KBD_IO5 TYPEWRT_PIN_KBD_IO5
#define PIN_KBD_IO4 TYPEWRT_PIN_KBD_IO4
#define PIN_KBD_IO3  TYPEWRT_PIN_KBD_IO3
#define PIN_KBD_IO2  TYPEWRT_PIN_KBD_IO2
#define PIN_KBD_IO1 TYPEWRT_PIN_KBD_IO1
#define PIN_KBD_IO0 TYPEWRT_PIN_KBD_IO0
#define PIN_KBD_OE TYPEWRT_PIN_KBD_OE
#define PIN_KBD_LE TYPEWRT_PIN_KBD_LE

#define SCANPERIOD 1500  // us  Minimum response time (min debounce/denoise) is 8 consecutive periods.

static const int KBD_IO[IOSIZE] = {PIN_KBD_IO0, PIN_KBD_IO1, PIN_KBD_IO2, PIN_KBD_IO3, 
	                             PIN_KBD_IO4, PIN_KBD_IO5, PIN_KBD_IO6, PIN_KBD_IO7};
static const uint32_t KBD_IO_MASK = ((1UL << PIN_KBD_IO0) | (1UL << PIN_KBD_IO1) | (1UL << PIN_KBD_IO2) |  
                                 (1UL << PIN_KBD_IO3) | (1UL << PIN_KBD_IO4) | (1UL << PIN_KBD_IO5) |  
                                 (1UL << PIN_KBD_IO6) | (1UL << PIN_KBD_IO7));
static const int KBD_OE = PIN_KBD_OE;
static const int KBD_LE = PIN_KBD_LE;
static esp_timer_handle_t KBD_SCAN_TIMER;
static volatile uint8_t KBD_COLS[IOSIZE];     // here uint8_t assuming 8 rows.
static volatile uint8_t KBD_COLFLAGS[IOSIZE];  // this is keeping the flag for scanning
static volatile uint8_t KBD_BUFFER[IOSIZE * IOSIZE]; /* Debounce history. */
static volatile bool KBD_NOKEY = true;

static inline IRAM_ATTR void cycle(uint32_t cycles)
{
    /* One cycle at 240 MHz is 4.16 ns; at 160 MHz it is 6.25 ns. */
    for (int i = 0; i < cycles; i++) {
        __asm__ __volatile__("nop");
    }
}

static IRAM_ATTR void kbd_scan(void *arg)
{
    (void)arg;

    KBD_NOKEY = true;
    for (int row = 0; row < IOSIZE; row++) {
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
        KBD_COLFLAGS[row] |= cols_in ^ KBD_COLS[row];
        uint8_t scan = KBD_COLFLAGS[row];
        while (scan) {
            int col = __builtin_ffs(scan) - 1;
            int idx = IOSIZE * row + col;

            KBD_BUFFER[idx] = (KBD_BUFFER[idx] << 1) | ((cols_in >> col) & 1);
            if (KBD_BUFFER[idx] == 0x00 && (KBD_COLS[row] & (1 << col))) {
                uint8_t key_event = KBDMAP[idx] | KEYDOWN_MASK;

                KBD_COLS[row] &= ~(1 << col);
                xQueueSend(keyboard, &key_event, 0);
                KBD_COLFLAGS[row] &= (~(1 << col) & ((1 << IOSIZE) - 1));
            }
            if (KBD_BUFFER[idx] == 0xff && !(KBD_COLS[row] & (1 << col))) {
                uint8_t key_event = KBDMAP[idx];

                KBD_COLS[row] |= (1 << col);
                xQueueSend(keyboard, &key_event, 0);
                KBD_COLFLAGS[row] &= (~(1 << col) & ((1 << IOSIZE) - 1));
            }
            scan &= scan - 1;
        }
        if ((~KBD_COLS[row]) & ((1UL << IOSIZE) - 1)) {
            KBD_NOKEY = false;
        }
    }
}

void typewrt_keyboard_start(void)
{
    if (!typewrt_keyboard_init()) {
        return;
    }

    if (!KBD_SCAN_TIMER) {
        const esp_timer_create_args_t scan_timer_args = {
            .callback = &kbd_scan,
            .name = "scanner",
        };

        ESP_ERROR_CHECK(esp_timer_create(&scan_timer_args, &KBD_SCAN_TIMER));
    }

    for (int k = 0; k < IOSIZE; k++) {
        gpio_reset_pin(KBD_IO[k]);
        KBD_COLS[k] = 0xff;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = KBD_IO_MASK,
        .intr_type = GPIO_INTR_DISABLE,  
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    gpio_config_t ol_config = {
        .pin_bit_mask = (1ULL << KBD_OE) | (1ULL << KBD_LE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&ol_config);
    gpio_sleep_sel_dis(KBD_OE);
    gpio_sleep_sel_dis(KBD_LE);

#if TYPEWRT_ENABLE_LIGHT_SLEEP
    for (int i = 0; i < IOSIZE; i++) {
        gpio_wakeup_enable(KBD_IO[i], GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
#endif

    ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));
}


bool typewrt_keyboard_idle(void)
{
    return keyboard && uxQueueMessagesWaiting(keyboard) == 0 && KBD_NOKEY;
}

void typewrt_keyboard_prepare_wakeup_rows(void)
{
    gpio_sleep_sel_dis(KBD_OE);
    gpio_sleep_sel_dis(KBD_LE);
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
}

esp_err_t typewrt_keyboard_stop_scanning(void)
{
    esp_err_t ret;

    if (!KBD_SCAN_TIMER) {
        return ESP_OK;
    }
    ret = esp_timer_stop(KBD_SCAN_TIMER);
    return ret == ESP_ERR_INVALID_STATE ? ESP_OK : ret;
}

esp_err_t typewrt_keyboard_start_scanning(void)
{
    if (!KBD_SCAN_TIMER) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD);
}

void typewrt_keyboard_disable_wakeup(void)
{
    for (int i = 0; i < IOSIZE; i++) {
        (void)gpio_wakeup_disable(KBD_IO[i]);
    }
}

void typewrt_keyboard_power_pins_high_z(void)
{
    uint64_t pin_mask = (uint64_t)KBD_IO_MASK |
        (1ULL << KBD_OE) |
        (1ULL << KBD_LE);
    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    gpio_config(&io_conf);
}
