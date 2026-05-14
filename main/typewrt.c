/*
 * SPDX-FileCopyrightText: 2020-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 *
 * Notes of LATCH SN74HC573A Texas Instruments: delays added during the latch operation are 
 * set acording to the datasheet time requirements. Although they are rated at 2V, 4.5V and 6V,
 * looking at the characteristic curve of the figure for the DQ delay, seems like the timing
 * requirements at 3.3V would be aprox. half way (linear) between the 2V and 4.5V values.
 * LE min pulse high: ~60 ns
 * Setup time, data, before LE LOW: ~40ns
 * Hold time, data, after LE LOW: ~10ns
 * Transmission D->Q: 130ns max, typ 50ns
 * Time to OEnable: 90ns max, typ 45ns
 * Time to ODisable: 90ns max, typ 45ns
 *
 */

#include <stdio.h>

#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
//#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
//#include "driver/gpio_filter.h"
#include "soc/gpio_struct.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "esp_private/gpio.h"
//#include "esp_task_wdt.h"
//#include "esp_intr_alloc.h"

#include "zap-vga16-raw-neg.h"
#include "keyboard_input.h"
#include "typewrt_splash.h"
#include "typewrt_power.h"
#undef MIN
#undef MAX
#include "vi.h"


/* DISPLAY DEFINITIONS */
#define PSF_GLYPH_SIZE 16

#define SHARPMEM_BIT_WRITECMD (0x01) // 0x80 in LSB format otherwise 0x01
#define SHARPMEM_BIT_VCOM (0x02)     // Sent now using SPI_DEVICE_TXBIT_LSBFIRST
#define SHARPMEM_BIT_CLEAR (0x04)

#define ESP_HOST    SPI2_HOST // SPI2
#define PIN_NUM_MOSI 35
#define PIN_NUM_MISO 37
#define PIN_NUM_CLK  36
#define PIN_NUM_CS   38
#define PIN_NUM_SD_CS 33
#define PIN_NUM_RTC_SDA 8
#define PIN_NUM_RTC_SCL 9
//#define PIN_NUM_VCOM   33
#define PIN_BLUE_LED   13

#define PXWIDTH 320
#define PXHEIGHT 240

#define PIN_LDO2_EN 39
#define PIN_LEDN 17
#define PIN_RST_EN 18
#define PIN_5V_EN 34



#define SHARPMEM_BYTES_PER_LINE (PXWIDTH / 8)
#define SHARPMEM_BUFFER_BYTES ((PXWIDTH * PXHEIGHT) / 8)

#define KEY(r, c) ((r << 3) + c)
#define CUR( x, y ) (x + y*PXWIDTH/8)  

#define KBD_EVENT_QUEUE_LENGTH 32
#define KBD_EVENT_SIZE sizeof( uint8_t )
#define TYPEWRT_ENABLE_LIGHT_SLEEP 1
#define TYPEWRT_LIGHT_SLEEP_IDLE_MS 1000
#define TYPEWRT_LED_BOOT_BLINKS 2
#define TYPEWRT_LED_BOOT_PULSE_US 250000
#define TYPEWRT_LED_SD_BLINK_PERIOD_US 75000
#define TYPEWRT_BATTERY_CHECK_HIGH_US (5ULL * 60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_CHECK_MED_US (2ULL * 60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_CHECK_LOW_US (60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_WARN_LOW_REPEAT_US (5ULL * 60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_WARN_CRIT_REPEAT_US (2ULL * 60ULL * 1000000ULL)
#define TYPEWRT_BATTERY_WARN_LOW_SOC_TENTHS 250
#define TYPEWRT_BATTERY_WARN_CRIT_SOC_TENTHS 100
#define TYPEWRT_BATTERY_WARN_PULSE_ON_US 150000
#define TYPEWRT_BATTERY_WARN_PULSE_GAP_US 850000
#define TYPEWRT_BATTERY_WARN_LOW_PULSES 3
#define TYPEWRT_BATTERY_WARN_CRIT_TOGGLE_US 100000
#define TYPEWRT_BATTERY_WARN_CRIT_DURATION_US 2000000
#define TYPEWRT_REFRESH_FULL_DISPLAY 0
#define TYPEWRT_SD_MOUNT_POINT "/sdcard"
#define TYPEWRT_RTC_I2C_PORT I2C_NUM_0
#define TYPEWRT_RTC_I2C_FREQ_HZ 100000
#define PCF8523_I2C_ADDRESS 0x68
#define PCF8523_CONTROL_1_REG 0x00
#define PCF8523_CONTROL_1_STOP BIT(5)
#define PCF8523_TIME_REG 0x03
#define PCF8523_SECONDS_OS BIT(7)
#define MAX17048_I2C_ADDRESS 0x36
#define MAX17048_VCELL_REG 0x02
#define MAX17048_SOC_REG 0x04
#define MAX17048_CRATE_REG 0x16
#define MAX17048_ABSENT_SOC_TENTHS 1050
#define MAX17048_ABSENT_VOLTAGE_UV 4350000

#define SPI_TAG "spi_protocol"
void displayInit(void);
void display_write_data(uint8_t addr, uint8_t data);


//// hold the queue structure.
///configSUPPORT_STATIC_ALLOCATION: Must be enabled in menuconfig?
StaticQueue_t kbd_StaticQueue;
uint8_t kbd_QueueStorage[ KBD_EVENT_QUEUE_LENGTH * KBD_EVENT_SIZE ];
QueueHandle_t keyboard;


spi_device_handle_t spi;
static sdmmc_card_t *sd_card;
static bool sd_card_mounted;
static i2c_master_bus_handle_t rtc_i2c_bus;
static i2c_master_dev_handle_t rtc_i2c_dev;
static i2c_master_dev_handle_t battery_i2c_dev;
static bool battery_gauge_available;
DMA_ATTR uint8_t *sharpmem_buffer = NULL;
static char display_shadow[NEXTVI_DISPLAY_ROWS + 1][NEXTVI_DISPLAY_COLS + 1];
static bool splash_active = true;
static bool splash_drawn;
static bool splash_disable_pending;
static bool display_cursor_drawn;
static int display_cursor_row = -1;
static int display_cursor_col = -1;
static esp_timer_handle_t splash_clock_timer;
static esp_timer_handle_t boot_led_timer;
static esp_timer_handle_t battery_led_timer;
static esp_timer_handle_t sd_write_led_timer;
static char splash_clock_last[24];
static char splash_status_last[NEXTVI_DISPLAY_COLS + 1];
static volatile uint32_t typewrt_sleep_locks;
static volatile uint32_t boot_led_steps;
static volatile bool boot_led_on;
static volatile bool battery_led_active;
static volatile bool battery_led_locked;
static volatile bool battery_led_on;
static volatile uint32_t battery_led_steps;
static uint32_t battery_led_on_us;
static uint32_t battery_led_off_us;
static int64_t battery_next_check_us;
static int64_t battery_next_warning_us;
static volatile int64_t typewrt_ui_wakeup_deadline_us;
static uint8_t battery_warning_level;
static volatile uint32_t typewrt_sd_write_locks;
static volatile bool sd_write_led_on;
static StaticSemaphore_t display_lock_storage;
static SemaphoreHandle_t display_lock;
bool typewrt_rtc_get_datetime(char *out, size_t out_len);
bool typewrt_battery_get_status(char *out, size_t out_len);
static void typewrt_light_sleep_if_idle(void);
static void typewrt_power_led_update(void);
static void typewrt_led_boot_blink_start(void);
static void typewrt_battery_monitor_check(bool force);
static void typewrt_timer_wakeup_prepare(void);
static void typewrt_battery_monitor_start(void);
static void typewrt_usb_wakeup_prepare(void);
static void typewrt_reset_button_enable(bool enabled);

typedef struct {
    uint32_t voltage_uv;
    uint32_t soc_tenths;
    int32_t rate_tenths;
    bool absent;
} typewrt_battery_sample_t;

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

void displayInit(void)
{
    // Start the VCOM toggling task
    //xTaskCreate(&vcom_toggle_task, "vcom", 2048, NULL, 5, NULL);

    esp_err_t ret;
    /* Allocate pixel buffer for SHARP DISP*/
    sharpmem_buffer = (uint8_t *)malloc(SHARPMEM_BUFFER_BYTES);
    if (!sharpmem_buffer) {
      printf("Error: sharpmem_buffer was NOT allocated\n\n");
      return;
    }
    display_lock = xSemaphoreCreateMutexStatic(&display_lock_storage);

    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);                   // Setting the CS' pin to work in OUTPUT mode

    spi_bus_config_t buscfg = {                                         // Provide details to the SPI_bus_sturcture of pins and maximum data size
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512 * 8                                       // 4095 bytes is the max size of data that can be sent because of hardware limitations
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2 * 1000 * 1000,                             // Clock out at 12 MHz
        .mode = 0,                                                      // SPI mode 0: CPOL:-0 and CPHA:-0
        .spics_io_num = -1,                                     // Control the CS ourselves
        .flags = (SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_3WIRE),
        .queue_size = 7,                                                // We want to be able to queue 7 transactions at a time
    };

    ret = spi_bus_initialize(ESP_HOST, &buscfg, SPI_DMA_CH_AUTO);       // Initialize the SPI bus
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(ESP_HOST, &devcfg, &spi);                  // Attach the Slave device to the SPI bus
    ESP_ERROR_CHECK(ret);
    printf("SPI initialized. MISO:%d MOSI:%d CLK:%d DISPLAY_CS:%d\n",
        PIN_NUM_MISO, PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
    gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);

    // Wait and clear display
    vTaskDelay(100 / portTICK_PERIOD_MS); 
}


// 1<<n is a costly operation on AVR -- table usu. smaller & faster
static const uint8_t  set[] = {1, 2, 4, 8, 16, 32, 64, 128},
                      clr[] = {(uint8_t)~1,  (uint8_t)~2,  (uint8_t)~4,
                              (uint8_t)~8,  (uint8_t)~16, (uint8_t)~32,
                              (uint8_t)~64, (uint8_t)~128};


void setPixel(int16_t x, int16_t y, uint16_t color) {
  if (color) {
    sharpmem_buffer[(y * PXWIDTH + x) / 8] |= set[x & 7]; // set[x & 7]
  } else {
    sharpmem_buffer[(y * PXWIDTH + x) / 8] &= clr[x & 7]; // clr[x & 7]
  }
}

uint8_t getPixel(uint16_t x, uint16_t y) {
  if ((x >= PXWIDTH) || (y >= PXHEIGHT))
    return 0; // <0 test not needed, unsigned
  return sharpmem_buffer[(y * PXWIDTH + x) / 8] & set[x & 7] ? 1 : 0;
}

static void sharpmem_select(uint32_t delay_us)
{
  gpio_set_level((gpio_num_t)PIN_NUM_CS, 1);
  esp_rom_delay_us(delay_us);
}

static void sharpmem_deselect(uint32_t delay_us)
{
  gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);
  esp_rom_delay_us(delay_us);
}

static void sharpmem_transmit(spi_transaction_t *t)
{
  esp_err_t ret;

  ret = spi_device_transmit(spi, t);
  assert(ret==ESP_OK);
}

static void sharpmem_start_write(spi_transaction_t *t, uint32_t delay_us)
{
  uint8_t write_data[1] = {(uint8_t)SHARPMEM_BIT_WRITECMD};

  memset(t, 0, sizeof(*t));
  sharpmem_select(delay_us);
  t->length = sizeof(write_data) * 8;
  t->tx_buffer = write_data;
  sharpmem_transmit(t);
}

static void sharpmem_finish_write(spi_transaction_t *t, uint32_t delay_us)
{
  int last_line[1] = {0x00};

  t->length = 8;
  t->tx_buffer = last_line;
  sharpmem_transmit(t);
  sharpmem_deselect(delay_us);
}

static void sharpmem_send_physical_line(spi_transaction_t *t, uint16_t physical_line)
{
  uint8_t line[SHARPMEM_BYTES_PER_LINE + 2];

  line[0] = (uint8_t)(physical_line + 1);
  memcpy(line + 1, sharpmem_buffer + physical_line * SHARPMEM_BYTES_PER_LINE,
      SHARPMEM_BYTES_PER_LINE);
  line[SHARPMEM_BYTES_PER_LINE + 1] = 0x00;

  t->length = (SHARPMEM_BYTES_PER_LINE + 2) * 8;
  t->tx_buffer = line;
  sharpmem_transmit(t);
}

void clearDisplay() {
  memset(sharpmem_buffer, 0xff, SHARPMEM_BUFFER_BYTES);
  sharpmem_select(6);
  uint8_t clear_data[2] = {(uint8_t)(SHARPMEM_BIT_CLEAR), 0x00};
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = sizeof(clear_data) * 8;
  t.tx_buffer = clear_data;
  esp_err_t ret = spi_device_polling_transmit(spi, &t);
  sharpmem_deselect(2);
  assert(ret==ESP_OK);
}

void refreshDisplay(void) {
  spi_transaction_t t;

  sharpmem_start_write(&t, 6);
  for (uint16_t physical_line = 0; physical_line < PXHEIGHT; physical_line++) {
    sharpmem_send_physical_line(&t, physical_line);
  }
  sharpmem_finish_write(&t, 2);
  }

void updateRow(uint8_t row) {
  spi_transaction_t t;
  uint16_t first_line = PSF_GLYPH_SIZE * row;

  sharpmem_start_write(&t, 1);
  for (uint16_t physical_line = first_line;
      physical_line < first_line + PSF_GLYPH_SIZE; physical_line++) {
    sharpmem_send_physical_line(&t, physical_line);
  }
  sharpmem_finish_write(&t, 1);
  }

static void updatePhysicalLine(uint16_t physical_line) {
  spi_transaction_t t;

  if (physical_line >= PXHEIGHT) {
    return;
  }
  sharpmem_start_write(&t, 1);
  sharpmem_send_physical_line(&t, physical_line);
  sharpmem_finish_write(&t, 1);
}

void clearDisplayBuffer() {
  memset(sharpmem_buffer, 0xFF, SHARPMEM_BUFFER_BYTES);
}

static void displayLock(void)
{
    if (display_lock) {
        xSemaphoreTake(display_lock, portMAX_DELAY);
    }
}

static bool displayTryLock(void)
{
    return !display_lock || xSemaphoreTake(display_lock, 0) == pdTRUE;
}

static void displayUnlock(void)
{
    if (display_lock) {
        xSemaphoreGive(display_lock);
    }
}

static void displayGlyph(uint8_t col, uint8_t row, uint8_t index)
{
    if (col >= NEXTVI_DISPLAY_COLS || row > NEXTVI_DISPLAY_ROWS) {
        return;
    }
    for (int m = 0; m < PSF_GLYPH_SIZE; m++) {
       sharpmem_buffer[((row * PSF_GLYPH_SIZE + m) * PXWIDTH + 8 * col) / 8] =
           zap_vga16_psf[index * PSF_GLYPH_SIZE + m];
    }
}

static uint8_t displayGlyphForCodepoint(uint32_t cp)
{
    if (cp >= 0x20 && cp <= 0x7e) {
        return (uint8_t)cp;
    }
    for (int i = 0; i < (int)(sizeof(unicodemap) / sizeof(unicodemap[0])); i++) {
        if ((uint32_t)unicodemap[i] == cp) {
            return fontmap[i];
        }
    }
    return fontmap[VK_REPLACEMENT - VKCHAROFFSET];
}

static uint8_t displayNextGlyph(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    uint32_t cp;

    if (!s[0]) {
        return ' ';
    }
    if (s[0] < 0x80) {
        (*text)++;
        return displayGlyphForCodepoint(s[0]);
    }
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1f) << 6) |
            (uint32_t)(s[1] & 0x3f);
        *text += 2;
        return displayGlyphForCodepoint(cp);
    }
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 &&
            (s[2] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0f) << 12) |
            ((uint32_t)(s[1] & 0x3f) << 6) |
            (uint32_t)(s[2] & 0x3f);
        *text += 3;
        return displayGlyphForCodepoint(cp);
    }
    if ((s[0] & 0xf8) == 0xf0 && (s[1] & 0xc0) == 0x80 &&
            (s[2] & 0xc0) == 0x80 && (s[3] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) |
            ((uint32_t)(s[1] & 0x3f) << 12) |
            ((uint32_t)(s[2] & 0x3f) << 6) |
            (uint32_t)(s[3] & 0x3f);
        *text += 4;
        return displayGlyphForCodepoint(cp);
    }
    (*text)++;
    return fontmap[VK_REPLACEMENT - VKCHAROFFSET];
}

static void markPhysicalRowRedrawn(int row)
{
    if (display_cursor_drawn && display_cursor_row == row) {
        display_cursor_drawn = false;
    }
}

static void copyDisplayShadow(int row, const char *text, int cols)
{
    const char *p = text ? text : "";

    if (row < 0 || row > NEXTVI_DISPLAY_ROWS) {
        return;
    }
    cols = cols > NEXTVI_DISPLAY_COLS ? NEXTVI_DISPLAY_COLS : cols;
    for (int col = 0; col < NEXTVI_DISPLAY_COLS; col++) {
        display_shadow[row][col] = col < cols ? displayNextGlyph(&p) : ' ';
    }
    display_shadow[row][NEXTVI_DISPLAY_COLS] = '\0';
}

static bool displayShadowRowHasVisibleText(int row)
{
    if (row < 0 || row > NEXTVI_DISPLAY_ROWS) {
        return false;
    }
    for (int col = 0; col < NEXTVI_DISPLAY_COLS; col++) {
        if (display_shadow[row][col] != ' ') {
            return true;
        }
    }
    return false;
}

static void renderTextRowMode(int physical_row, const char *text, bool inverted)
{
    const char *p = text ? text : "";

    if (!sharpmem_buffer || physical_row < 0 || physical_row > NEXTVI_DISPLAY_ROWS) {
        return;
    }

    for (int col = 0; col < NEXTVI_DISPLAY_COLS; col++) {
        displayGlyph(col, physical_row, displayNextGlyph(&p));
    }
    if (inverted) {
        for (int m = 0; m < PSF_GLYPH_SIZE; m++) {
            uint8_t *line = sharpmem_buffer +
                (physical_row * PSF_GLYPH_SIZE + m) * SHARPMEM_BYTES_PER_LINE;
            for (int byte = 0; byte < SHARPMEM_BYTES_PER_LINE; byte++) {
                line[byte] ^= 0xff;
            }
        }
    }
    markPhysicalRowRedrawn(physical_row);
#if TYPEWRT_REFRESH_FULL_DISPLAY
    refreshDisplay();
#else
    updateRow((uint8_t)physical_row);
#endif
}

static void renderGlyphRowMode(int physical_row, const char *glyphs, bool inverted)
{
    if (!sharpmem_buffer || physical_row < 0 || physical_row > NEXTVI_DISPLAY_ROWS) {
        return;
    }

    for (int col = 0; col < NEXTVI_DISPLAY_COLS; col++) {
        unsigned char glyph = glyphs && glyphs[col] ? (unsigned char)glyphs[col] : ' ';
        displayGlyph(col, physical_row, glyph);
    }
    if (inverted) {
        for (int m = 0; m < PSF_GLYPH_SIZE; m++) {
            uint8_t *line = sharpmem_buffer +
                (physical_row * PSF_GLYPH_SIZE + m) * SHARPMEM_BYTES_PER_LINE;
            for (int byte = 0; byte < SHARPMEM_BYTES_PER_LINE; byte++) {
                line[byte] ^= 0xff;
            }
        }
    }
    markPhysicalRowRedrawn(physical_row);
#if TYPEWRT_REFRESH_FULL_DISPLAY
    refreshDisplay();
#else
    updateRow((uint8_t)physical_row);
#endif
}

static void invertTextCell(int row, int col)
{
    for (int m = 0; m < PSF_GLYPH_SIZE; m++) {
        sharpmem_buffer[((row * PSF_GLYPH_SIZE + m) * PXWIDTH + 8 * col) / 8] ^= 0xff;
    }
}

static void renderTextRow(int physical_row, const char *text)
{
	renderTextRowMode(physical_row, text, false);
}

static void renderGlyphRow(int physical_row, const char *glyphs)
{
    renderGlyphRowMode(physical_row, glyphs, false);
}

static void renderBlankTextRow(int physical_row)
{
    static const char blank[NEXTVI_DISPLAY_COLS + 1] = "                                        ";

    renderTextRow(physical_row, blank);
}

static void splashBatteryStatus(char *out, size_t out_len)
{
    char status[96] = "";
    char state[16] = "";
    int whole, frac;
    char marker = typewrt_usb_power_present() ? 'C' : 'D';

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!typewrt_battery_get_status(status, sizeof(status))) {
        snprintf(out, out_len, "%c --%%", marker);
        return;
    }
    if (!strncmp(status, "bat absent", 10)) {
        snprintf(out, out_len, "%c --%%", marker);
        return;
    }
    if (sscanf(status, "bat %d.%d%% %*s %15s", &whole, &frac, state) == 3) {
        if (!strcmp(state, "chg")) {
            marker = 'C';
        } else if (!strcmp(state, "dis")) {
            marker = 'D';
        }
        whole += frac >= 5 ? 1 : 0;
        if (whole < 0) {
            whole = 0;
        }
        if (whole > 100) {
            whole = 100;
        }
        snprintf(out, out_len, "%c %d%%", marker, whole);
        return;
    }
    snprintf(out, out_len, "%c --%%", marker);
}

static void splashStatusLine(char line[NEXTVI_DISPLAY_COLS + 1])
{
    char timestamp[24];
    char battery[16];
    char right[64];
    size_t ts_len;
    size_t right_len;

    memcpy(line, display_shadow[NEXTVI_DISPLAY_ROWS], NEXTVI_DISPLAY_COLS);
    line[NEXTVI_DISPLAY_COLS] = '\0';
    if (!typewrt_rtc_get_datetime(timestamp, sizeof(timestamp))) {
        return;
    }
    splashBatteryStatus(battery, sizeof(battery));
    snprintf(right, sizeof(right), "%s  %s", battery, timestamp);
    right_len = strlen(right);
    ts_len = strlen(timestamp);
    if (right_len > NEXTVI_DISPLAY_COLS || ts_len > NEXTVI_DISPLAY_COLS) {
        return;
    }
    memcpy(line + NEXTVI_DISPLAY_COLS - right_len, right, right_len);
}

static void splash_schedule_status_wakeup(void)
{
    time_t now = time(NULL);
    int seconds = 60 - (int)(now % 60);

    if (!splash_active || !splash_drawn) {
        return;
    }
    if (seconds <= 0 || seconds > 60) {
        seconds = 60;
    }
    typewrt_sleep_set_ui_wakeup_us((uint64_t)seconds * 1000000ULL);
}

static void refreshSplashStatusClock(void)
{
    char timestamp[sizeof(splash_clock_last)];
    char status_line[NEXTVI_DISPLAY_COLS + 1];

    typewrt_sleep_lock();
    if (!displayTryLock()) {
        goto done_sleep;
    }
    if (!splash_active || !splash_drawn) {
        goto done;
    }
    splash_schedule_status_wakeup();
    if (!typewrt_rtc_get_datetime(timestamp, sizeof(timestamp))) {
        goto done;
    }
    if (!strcmp(timestamp, splash_clock_last)) {
        goto done;
    }
    strncpy(splash_clock_last, timestamp, sizeof(splash_clock_last) - 1);
    splash_clock_last[sizeof(splash_clock_last) - 1] = '\0';
    splashStatusLine(status_line);
    if (!strcmp(status_line, splash_status_last)) {
        goto done;
    }
    strncpy(splash_status_last, status_line, sizeof(splash_status_last) - 1);
    splash_status_last[sizeof(splash_status_last) - 1] = '\0';
    renderTextRow(NEXTVI_DISPLAY_ROWS, status_line);
done:
    displayUnlock();
done_sleep:
    typewrt_sleep_unlock();
}

static void splashClockTimerCallback(void *arg)
{
    (void)arg;
    refreshSplashStatusClock();
}

static void renderSplashBitmapRow(int text_row)
{
    int bytes_per_line = PXWIDTH / 8;
    int physical_line = text_row * PSF_GLYPH_SIZE;

    if (!sharpmem_buffer || text_row < 0 || text_row >= TYPEWRT_SPLASH_HEIGHT / PSF_GLYPH_SIZE) {
        return;
    }
    for (int m = 0; m < PSF_GLYPH_SIZE; m++) {
        memcpy(sharpmem_buffer + (physical_line + m) * bytes_per_line,
            typewrt_splash_bits + (physical_line + m) * bytes_per_line,
            bytes_per_line);
    }
    markPhysicalRowRedrawn(text_row);
    updateRow((uint8_t)text_row);
}

static int mapSplashRow(int row)
{
    return splash_active && row == 0 ? NEXTVI_DISPLAY_ROWS - 1 : row;
}

static int cursorCellValid(int row, int col);
static void invertCursorCell(int row, int col);

static void drawSplashLayout(void)
{
    char status_line[NEXTVI_DISPLAY_COLS + 1];

    if (!splash_active || splash_drawn) {
        return;
    }
    for (int row = 0; row < TYPEWRT_SPLASH_HEIGHT / PSF_GLYPH_SIZE; row++) {
        renderSplashBitmapRow(row);
    }
    for (int row = TYPEWRT_SPLASH_HEIGHT / PSF_GLYPH_SIZE; row < NEXTVI_DISPLAY_ROWS - 1; row++) {
        renderBlankTextRow(row);
    }
    renderGlyphRow(NEXTVI_DISPLAY_ROWS - 1, display_shadow[0]);
    splashStatusLine(status_line);
    renderTextRow(NEXTVI_DISPLAY_ROWS, status_line);
    strncpy(splash_status_last, status_line, sizeof(splash_status_last) - 1);
    splash_status_last[sizeof(splash_status_last) - 1] = '\0';
    typewrt_rtc_get_datetime(splash_clock_last, sizeof(splash_clock_last));
    splash_drawn = true;
    splash_schedule_status_wakeup();
}

static void disableSplash(void)
{
    if (!splash_active) {
        return;
    }
    if (splash_clock_timer) {
        (void)esp_timer_stop(splash_clock_timer);
    }
    typewrt_sleep_clear_ui_wakeup();
    if (display_cursor_drawn && cursorCellValid(display_cursor_row, display_cursor_col)) {
        invertCursorCell(display_cursor_row, display_cursor_col);
    }
    splash_active = false;
    splash_drawn = false;
    splash_disable_pending = false;
    display_cursor_drawn = false;
    display_cursor_row = -1;
    display_cursor_col = -1;
    for (int row = 0; row <= NEXTVI_DISPLAY_ROWS; row++) {
        renderGlyphRow(row, display_shadow[row]);
    }
    typewrt_reset_button_enable(false);
}

void nextvi_display_refresh_line(int row, const char *text, int cols)
{
    if (!sharpmem_buffer || row < 0 || row > NEXTVI_DISPLAY_ROWS) {
        return;
    }

    displayLock();
    copyDisplayShadow(row, text, cols);
    if (splash_active && splash_disable_pending && row == 0 &&
            displayShadowRowHasVisibleText(row)) {
        disableSplash();
        goto done;
    }
    if (splash_active) {
        drawSplashLayout();
        if (row == 0) {
            renderGlyphRow(NEXTVI_DISPLAY_ROWS - 1, display_shadow[0]);
        } else if (row == NEXTVI_DISPLAY_ROWS) {
            char status_line[NEXTVI_DISPLAY_COLS + 1];
            splashStatusLine(status_line);
            renderTextRow(row, status_line);
        }
        goto done;
    }

    renderGlyphRow(row, display_shadow[row]);
done:
    displayUnlock();
}

void nextvi_display_refresh_line_inverted(int row, const char *text, int cols)
{
    if (!sharpmem_buffer || row < 0 || row > NEXTVI_DISPLAY_ROWS) {
        return;
    }

    displayLock();
    copyDisplayShadow(row, text, cols);
    if (splash_active) {
        disableSplash();
    }
    renderGlyphRowMode(row, display_shadow[row], true);
	displayUnlock();
}

void nextvi_display_refresh_line_attrs(int row, const char *text,
    const unsigned char *attrs, int cols)
{
    if (!sharpmem_buffer || row < 0 || row > NEXTVI_DISPLAY_ROWS) {
        return;
    }

    displayLock();
    copyDisplayShadow(row, text, cols);
    if (splash_active && splash_disable_pending && row == 0 &&
            displayShadowRowHasVisibleText(row)) {
        disableSplash();
        goto done;
    }
    if (splash_active) {
        drawSplashLayout();
        if (row == 0) {
            renderGlyphRow(NEXTVI_DISPLAY_ROWS - 1, display_shadow[0]);
        } else if (row == NEXTVI_DISPLAY_ROWS) {
            char status_line[NEXTVI_DISPLAY_COLS + 1];
            splashStatusLine(status_line);
            renderTextRow(row, status_line);
        }
        goto done;
    }

    for (int col = 0; col < NEXTVI_DISPLAY_COLS; col++) {
        unsigned char glyph = display_shadow[row][col] ?
            (unsigned char)display_shadow[row][col] : ' ';
        displayGlyph(col, row, glyph);
    }
    if (attrs) {
        for (int col = 0; col < NEXTVI_DISPLAY_COLS; col++) {
            if (attrs[col]) {
                invertTextCell(row, col);
            }
        }
    }
    markPhysicalRowRedrawn(row);
#if TYPEWRT_REFRESH_FULL_DISPLAY
    refreshDisplay();
#else
    updateRow((uint8_t)row);
#endif
done:
    displayUnlock();
}

void nextvi_display_draw_hline(int y, int color)
{
    if (!sharpmem_buffer || y < 0 || y >= PXHEIGHT) {
        return;
    }

    displayLock();
    if (splash_active) {
        disableSplash();
    }
    memset(sharpmem_buffer + y * SHARPMEM_BYTES_PER_LINE,
        color ? 0xff : 0x00, SHARPMEM_BYTES_PER_LINE);
    markPhysicalRowRedrawn(y / PSF_GLYPH_SIZE);
    updatePhysicalLine((uint16_t)y);
    displayUnlock();
}

static int cursorCellValid(int row, int col)
{
    return sharpmem_buffer && row >= 0 && row <= NEXTVI_DISPLAY_ROWS &&
        col >= 0 && col < NEXTVI_DISPLAY_COLS;
}

static void invertCursorCell(int row, int col)
{
    invertTextCell(row, col);
}

void nextvi_display_refresh_cursor(int row, int col, int on)
{
    bool old_valid;
    int old_row;

    displayLock();
    if (!on) {
        if (display_cursor_drawn) {
            invertCursorCell(display_cursor_row, display_cursor_col);
            updateRow((uint8_t)display_cursor_row);
            display_cursor_drawn = false;
        }
        goto done;
    }

    row = mapSplashRow(row);
    if (!cursorCellValid(row, col)) {
        goto done;
    }
    old_valid = display_cursor_drawn &&
        cursorCellValid(display_cursor_row, display_cursor_col);
    old_row = display_cursor_row;
    if (old_valid && display_cursor_row == row && display_cursor_col == col) {
        goto done;
    }
    if (old_valid) {
        invertCursorCell(display_cursor_row, display_cursor_col);
    }
    invertCursorCell(row, col);
    if (old_valid) {
        updateRow((uint8_t)old_row);
    }
    if (!old_valid || old_row != row) {
        updateRow((uint8_t)row);
    }
    display_cursor_row = row;
    display_cursor_col = col;
    display_cursor_drawn = true;
done:
    displayUnlock();
}

void nextvi_display_move_cursor(int old_row, int old_col, int new_row, int new_col, int on)
{
    bool old_valid;
    int new_physical_row;
    bool new_valid;

    displayLock();
    old_valid = display_cursor_drawn &&
        cursorCellValid(display_cursor_row, display_cursor_col);
    new_physical_row = mapSplashRow(new_row);
    new_valid = on && cursorCellValid(new_physical_row, new_col);

    if (!old_valid && !new_valid) {
        goto done;
    }
    if (old_valid && new_valid && display_cursor_row == new_physical_row &&
            display_cursor_col == new_col) {
        goto done;
    }
    if (old_valid) {
        invertCursorCell(display_cursor_row, display_cursor_col);
    }
    if (new_valid) {
        invertCursorCell(new_physical_row, new_col);
    }
    if (old_valid) {
        updateRow((uint8_t)display_cursor_row);
    }
    if (new_valid && (!old_valid || new_physical_row != display_cursor_row)) {
        updateRow((uint8_t)new_physical_row);
    }
    display_cursor_drawn = new_valid;
    display_cursor_row = new_valid ? new_physical_row : -1;
    display_cursor_col = new_valid ? new_col : -1;
done:
    displayUnlock();
    (void)old_row;
    (void)old_col;
}

void nextvi_display_note_insert(void)
{
    displayLock();
    if (displayShadowRowHasVisibleText(0)) {
        disableSplash();
    } else {
        splash_disable_pending = true;
    }
    displayUnlock();
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

static void vNextviTask(void *pvParameters)
{
    char *argv[] = {"vi"};

    (void)pvParameters;
    if (sd_card_mounted) {
        setenv("PWD", TYPEWRT_SD_MOUNT_POINT, 1);
    }
    nextvi_main(1, argv);
    vTaskDelete(NULL);
}



static const char *TAG = "mkbd";

static int bcd_to_dec(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0f);
}

static uint8_t dec_to_bcd(int value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t rtc_pcf8523_read_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(rtc_i2c_dev, &reg, sizeof(reg),
        value, sizeof(*value), 1000);
}

static esp_err_t rtc_pcf8523_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};

    return i2c_master_transmit(rtc_i2c_dev, data, sizeof(data), 1000);
}

static esp_err_t rtc_pcf8523_clear_bits(uint8_t reg, uint8_t bits)
{
    uint8_t value;
    esp_err_t ret = rtc_pcf8523_read_reg(reg, &value);

    if (ret != ESP_OK) {
        return ret;
    }
    return rtc_pcf8523_write_reg(reg, value & ~bits);
}

static esp_err_t rtc_pcf8523_start_oscillator(void)
{
    return rtc_pcf8523_clear_bits(PCF8523_CONTROL_1_REG, PCF8523_CONTROL_1_STOP);
}

static esp_err_t rtc_pcf8523_write_time(const struct tm *rtc_tm)
{
    uint8_t data[8] = {
        PCF8523_TIME_REG,
        dec_to_bcd(rtc_tm->tm_sec),
        dec_to_bcd(rtc_tm->tm_min),
        dec_to_bcd(rtc_tm->tm_hour),
        dec_to_bcd(rtc_tm->tm_mday),
        dec_to_bcd(rtc_tm->tm_wday),
        dec_to_bcd(rtc_tm->tm_mon + 1),
        dec_to_bcd((rtc_tm->tm_year + 1900) % 100),
    };

    return i2c_master_transmit(rtc_i2c_dev, data, sizeof(data), 1000);
}

static bool rtc_time_is_valid(const struct tm *rtc_tm)
{
    int year = rtc_tm->tm_year + 1900;

    return year >= 2024 && year <= 2099 &&
        rtc_tm->tm_mon >= 0 && rtc_tm->tm_mon <= 11 &&
        rtc_tm->tm_mday >= 1 && rtc_tm->tm_mday <= 31 &&
        rtc_tm->tm_hour >= 0 && rtc_tm->tm_hour <= 23 &&
        rtc_tm->tm_min >= 0 && rtc_tm->tm_min <= 59 &&
        rtc_tm->tm_sec >= 0 && rtc_tm->tm_sec <= 59;
}

static bool rtc_pcf8523_read_regs(uint8_t data[7])
{
    uint8_t reg = PCF8523_TIME_REG;
    esp_err_t ret = i2c_master_transmit_receive(rtc_i2c_dev, &reg, sizeof(reg),
        data, 7, 1000);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 read failed: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

static void rtc_pcf8523_regs_to_tm(const uint8_t data[7], struct tm *rtc_tm)
{
    memset(rtc_tm, 0, sizeof(*rtc_tm));
    rtc_tm->tm_sec = bcd_to_dec(data[0] & 0x7f);
    rtc_tm->tm_min = bcd_to_dec(data[1] & 0x7f);
    rtc_tm->tm_hour = bcd_to_dec(data[2] & 0x3f);
    rtc_tm->tm_mday = bcd_to_dec(data[3] & 0x3f);
    rtc_tm->tm_wday = bcd_to_dec(data[4] & 0x07);
    rtc_tm->tm_mon = bcd_to_dec(data[5] & 0x1f) - 1;
    rtc_tm->tm_year = bcd_to_dec(data[6]) + 100;
    rtc_tm->tm_isdst = -1;
}

static bool rtc_pcf8523_read_time(struct tm *rtc_tm)
{
    uint8_t data[7];
    bool oscillator_stop;

    if (!rtc_pcf8523_read_regs(data)) {
        return false;
    }

    oscillator_stop = data[0] & PCF8523_SECONDS_OS;
    rtc_pcf8523_regs_to_tm(data, rtc_tm);

    if (!rtc_time_is_valid(rtc_tm)) {
        ESP_LOGW(TAG, "PCF8523 returned an invalid date/time");
        return false;
    }

    if (oscillator_stop) {
        esp_err_t ret = rtc_pcf8523_write_reg(PCF8523_TIME_REG,
            data[0] & ~PCF8523_SECONDS_OS);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "PCF8523 failed to clear oscillator-stop flag: %s",
                esp_err_to_name(ret));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!rtc_pcf8523_read_regs(data)) {
            return false;
        }
        if (data[0] & PCF8523_SECONDS_OS) {
            ESP_LOGW(TAG, "PCF8523 oscillator-stop flag is set; RTC time is not trusted");
            return false;
        }
        rtc_pcf8523_regs_to_tm(data, rtc_tm);
        if (!rtc_time_is_valid(rtc_tm)) {
            ESP_LOGW(TAG, "PCF8523 returned an invalid date/time after clearing OS flag");
            return false;
        }
        ESP_LOGI(TAG, "PCF8523 oscillator-stop flag was set and has been cleared");
    }

    return true;
}

static bool rtc_parse_datetime(const char *datetime, struct tm *rtc_tm)
{
    int year, month, day, hour, minute, second, used = 0;

    if (sscanf(datetime, "%d-%d-%d %d:%d:%d%n",
            &year, &month, &day, &hour, &minute, &second, &used) != 6 &&
        sscanf(datetime, "%d-%d-%d %d.%d.%d%n",
            &year, &month, &day, &hour, &minute, &second, &used) != 6 &&
        sscanf(datetime, "%d-%d-%d %d %d %d%n",
            &year, &month, &day, &hour, &minute, &second, &used) != 6) {
        return false;
    }
    while (isspace((unsigned char)datetime[used])) {
        used++;
    }
    if (datetime[used]) {
        return false;
    }

    memset(rtc_tm, 0, sizeof(*rtc_tm));
    rtc_tm->tm_year = year - 1900;
    rtc_tm->tm_mon = month - 1;
    rtc_tm->tm_mday = day;
    rtc_tm->tm_hour = hour;
    rtc_tm->tm_min = minute;
    rtc_tm->tm_sec = second;
    rtc_tm->tm_isdst = -1;
    if (!rtc_time_is_valid(rtc_tm)) {
        return false;
    }

    time_t normalized = mktime(rtc_tm);
    if (normalized == (time_t)-1) {
        return false;
    }
    return rtc_tm->tm_year == year - 1900 &&
        rtc_tm->tm_mon == month - 1 &&
        rtc_tm->tm_mday == day &&
        rtc_tm->tm_hour == hour &&
        rtc_tm->tm_min == minute &&
        rtc_tm->tm_sec == second;
}

static bool rtc_set_system_time(const struct tm *rtc_tm)
{
    struct tm tm_copy = *rtc_tm;
    time_t rtc_time = mktime(&tm_copy);
    if (rtc_time == (time_t)-1) {
        ESP_LOGW(TAG, "RTC time conversion failed");
        return false;
    }

    struct timeval tv = {
        .tv_sec = rtc_time,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "settimeofday from RTC failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool typewrt_rtc_get_datetime(char *out, size_t out_len)
{
    struct tm rtc_tm;

    if (!out || out_len == 0) {
        return false;
    }
    if (!rtc_i2c_dev || !rtc_pcf8523_read_time(&rtc_tm)) {
        time_t now = time(NULL);
        localtime_r(&now, &rtc_tm);
    }
    return strftime(out, out_len, "%d %b %Y  %H:%M", &rtc_tm) > 0;
}

const char *typewrt_rtc_set_datetime(const char *datetime, char *out, size_t out_len)
{
    struct tm rtc_tm;
    esp_err_t ret;

    if (!rtc_i2c_dev) {
        return "rtc unavailable";
    }
    if (!rtc_parse_datetime(datetime, &rtc_tm)) {
        return "rtc syntax: YYYY-MM-DD HH.MM.SS";
    }
    ret = rtc_pcf8523_start_oscillator();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 start oscillator failed: %s", esp_err_to_name(ret));
        return "rtc start failed";
    }
    ret = rtc_pcf8523_write_time(&rtc_tm);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 write time failed: %s", esp_err_to_name(ret));
        return "rtc write failed";
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!rtc_pcf8523_read_time(&rtc_tm)) {
        return "rtc verify failed";
    }
    if (!rtc_set_system_time(&rtc_tm)) {
        return "system clock failed";
    }
    if (out && out_len) {
        typewrt_rtc_get_datetime(out, out_len);
    }
    return NULL;
}

static bool rtc_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = TYPEWRT_RTC_I2C_PORT,
        .sda_io_num = PIN_NUM_RTC_SDA,
        .scl_io_num = PIN_NUM_RTC_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_config, &rtc_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C init for RTC failed: %s", esp_err_to_name(ret));
        return false;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF8523_I2C_ADDRESS,
        .scl_speed_hz = TYPEWRT_RTC_I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(rtc_i2c_bus, &dev_config, &rtc_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 add-device failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = i2c_master_probe(rtc_i2c_bus, PCF8523_I2C_ADDRESS, 1000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 not found at I2C address 0x%02x: %s",
            PCF8523_I2C_ADDRESS, esp_err_to_name(ret));
        return false;
    }

    struct tm rtc_tm;
    if (!rtc_pcf8523_read_time(&rtc_tm)) {
        return false;
    }

    if (!rtc_set_system_time(&rtc_tm)) {
        return false;
    }

    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &rtc_tm);
    ESP_LOGI(TAG, "System clock set from PCF8523: %s", time_buf);
    return true;
}

static esp_err_t battery_max17048_read_word(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    esp_err_t ret;

    if (!battery_i2c_dev || !value) {
        return ESP_ERR_INVALID_STATE;
    }
    ret = i2c_master_transmit_receive(battery_i2c_dev, &reg, sizeof(reg),
        data, sizeof(data), 1000);
    if (ret != ESP_OK) {
        return ret;
    }
    *value = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

static esp_err_t typewrt_battery_read_sample(typewrt_battery_sample_t *sample)
{
    uint16_t vcell_raw;
    uint16_t soc_raw;
    uint16_t crate_raw;
    int16_t crate;

    if (!sample) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!battery_gauge_available || !battery_i2c_dev) {
        return ESP_ERR_INVALID_STATE;
    }

    if (battery_max17048_read_word(MAX17048_VCELL_REG, &vcell_raw) != ESP_OK ||
            battery_max17048_read_word(MAX17048_SOC_REG, &soc_raw) != ESP_OK ||
            battery_max17048_read_word(MAX17048_CRATE_REG, &crate_raw) != ESP_OK) {
        return ESP_FAIL;
    }

    sample->voltage_uv = (uint32_t)(((uint64_t)vcell_raw * 78125U) / 1000U);
    sample->soc_tenths = ((uint32_t)soc_raw * 10U + 128U) / 256U;
    sample->absent = sample->soc_tenths > MAX17048_ABSENT_SOC_TENTHS ||
        sample->voltage_uv > MAX17048_ABSENT_VOLTAGE_UV;
    crate = (int16_t)crate_raw;
    sample->rate_tenths = ((int32_t)crate * 208 + (crate >= 0 ? 50 : -50)) / 100;
    return ESP_OK;
}

bool typewrt_battery_get_status(char *out, size_t out_len)
{
    typewrt_battery_sample_t sample;
    esp_err_t ret;
    uint32_t rate_abs_tenths;
    const char *state;
    const char *rate_sign;

    if (!out || out_len == 0) {
        return false;
    }
    typewrt_sleep_lock();
    ret = typewrt_battery_read_sample(&sample);
    if (ret == ESP_ERR_INVALID_STATE) {
        snprintf(out, out_len, "battery unavailable");
        typewrt_sleep_unlock();
        return false;
    }
    if (ret != ESP_OK) {
        snprintf(out, out_len, "battery read failed");
        typewrt_sleep_unlock();
        return false;
    }

    if (sample.absent) {
        snprintf(out, out_len,
            "bat absent %" PRIu32 ".%03" PRIu32 "V soc %" PRIu32 ".%" PRIu32 "%%",
            sample.voltage_uv / 1000000U, (sample.voltage_uv % 1000000U) / 1000U,
            sample.soc_tenths / 10U, sample.soc_tenths % 10U);
        typewrt_sleep_unlock();
        return true;
    }

    rate_abs_tenths = sample.rate_tenths < 0 ?
        (uint32_t)-sample.rate_tenths : (uint32_t)sample.rate_tenths;
    if (sample.rate_tenths > 0) {
        state = "chg";
    } else if (sample.rate_tenths < 0) {
        state = "dis";
    } else {
        state = "idle";
    }
    rate_sign = sample.rate_tenths < 0 ? "-" : sample.rate_tenths > 0 ? "+" : "";

    snprintf(out, out_len,
        "bat %" PRIu32 ".%" PRIu32 "%% %" PRIu32 ".%03" PRIu32
        "V %s %s%" PRIu32 ".%" PRIu32 "%%/h",
        sample.soc_tenths / 10U, sample.soc_tenths % 10U,
        sample.voltage_uv / 1000000U, (sample.voltage_uv % 1000000U) / 1000U,
        state, rate_sign, rate_abs_tenths / 10U, rate_abs_tenths % 10U);
    typewrt_sleep_unlock();
    return true;
}

static bool battery_init(void)
{
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX17048_I2C_ADDRESS,
        .scl_speed_hz = TYPEWRT_RTC_I2C_FREQ_HZ,
    };
    esp_err_t ret;
    char status[96];

    if (!rtc_i2c_bus) {
        ESP_LOGW(TAG, "MAX17048 skipped; I2C bus is unavailable");
        return false;
    }
    ret = i2c_master_bus_add_device(rtc_i2c_bus, &dev_config, &battery_i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MAX17048 add-device failed: %s", esp_err_to_name(ret));
        return false;
    }
    ret = i2c_master_probe(rtc_i2c_bus, MAX17048_I2C_ADDRESS, 1000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MAX17048 not found at I2C address 0x%02x: %s",
            MAX17048_I2C_ADDRESS, esp_err_to_name(ret));
        (void)i2c_master_bus_rm_device(battery_i2c_dev);
        battery_i2c_dev = NULL;
        return false;
    }

    battery_gauge_available = true;
    if (typewrt_battery_get_status(status, sizeof(status))) {
        ESP_LOGI(TAG, "MAX17048 %s", status);
    }
    typewrt_battery_monitor_start();
    return true;
}

static void sdcard_spi_pins_prepare(void)
{
    gpio_config_t cs_conf = {
        .pin_bit_mask = 1ULL << PIN_NUM_SD_CS,
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&cs_conf);
    gpio_set_level(PIN_NUM_SD_CS, 1);

    gpio_set_pull_mode(PIN_NUM_MISO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_NUM_MOSI, GPIO_PULLUP_ONLY);
}

static bool sdcard_write_probe(void)
{
    const char *path = TYPEWRT_SD_MOUNT_POINT "/.typewrt_probe";
    const char probe_text[] = "typewrt sd probe\n";
    char readback[sizeof(probe_text)] = {0};

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        ESP_LOGE(TAG, "SD card write probe create failed: %s", strerror(errno));
        return false;
    }
    ssize_t written = write(fd, probe_text, sizeof(probe_text) - 1);
    if (written != sizeof(probe_text) - 1) {
        ESP_LOGE(TAG, "SD card write probe write failed: %s", strerror(errno));
        close(fd);
        return false;
    }
    close(fd);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "SD card write probe reopen failed: %s", strerror(errno));
        return false;
    }
    ssize_t read_len = read(fd, readback, sizeof(probe_text) - 1);
    close(fd);
    unlink(path);
    if (read_len != sizeof(probe_text) - 1 ||
            memcmp(readback, probe_text, sizeof(probe_text) - 1) != 0) {
        ESP_LOGE(TAG, "SD card write probe readback mismatch");
        return false;
    }

    ESP_LOGI(TAG, "SD card write probe succeeded");
    return true;
}

static bool sdcard_init(void)
{
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();

    host.slot = ESP_HOST;
    host.max_freq_khz = 10000;
    host.unaligned_multi_block_rw_max_chunk_size = 8;

    slot_config.gpio_cs = PIN_NUM_SD_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting SD card at %s on SPI host %d, CS:%d",
        TYPEWRT_SD_MOUNT_POINT, host.slot, PIN_NUM_SD_CS);
    esp_err_t ret = esp_vfs_fat_sdspi_mount(TYPEWRT_SD_MOUNT_POINT,
        &host, &slot_config, &mount_config, &sd_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount FAT filesystem on SD card");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        return false;
    }

    sd_card_mounted = true;
    sdmmc_card_print_info(stdout, sd_card);
    setenv("PWD", TYPEWRT_SD_MOUNT_POINT, 1);
    sdcard_write_probe();
    ESP_LOGI(TAG, "SD card mounted; Nextvi file paths are relative to %s",
        TYPEWRT_SD_MOUNT_POINT);
    return true;
}

#define IOSIZE 8

#define PIN_KBD_IO7  1
#define PIN_KBD_IO6  7
#define PIN_KBD_IO5 10
#define PIN_KBD_IO4 11
#define PIN_KBD_IO3  5
#define PIN_KBD_IO2  6
#define PIN_KBD_IO1 12
#define PIN_KBD_IO0 14
#define PIN_KBD_OE 16
#define PIN_KBD_LE 15

#define SCANTIMEOUT 500    // in number of scans
#define SCANPERIOD 1500  // us  Minimum response time (min debounce/denoise) is 8 consecutive periods.

const volatile int KBD_IO[IOSIZE] = {PIN_KBD_IO0, PIN_KBD_IO1, PIN_KBD_IO2, PIN_KBD_IO3, 
	                             PIN_KBD_IO4, PIN_KBD_IO5, PIN_KBD_IO6, PIN_KBD_IO7};
volatile uint32_t KBD_IO_MASK = ((1UL << PIN_KBD_IO0) | (1UL << PIN_KBD_IO1) | (1UL << PIN_KBD_IO2) |  
                                 (1UL << PIN_KBD_IO3) | (1UL << PIN_KBD_IO4) | (1UL << PIN_KBD_IO5) |  
                                 (1UL << PIN_KBD_IO6) | (1UL << PIN_KBD_IO7));
const volatile int KBD_OE = PIN_KBD_OE;
const volatile int KBD_LE = PIN_KBD_LE;
esp_timer_handle_t KBD_SCAN_TIMER;
volatile uint8_t KBD_COLS[IOSIZE];     // here uint8_t assuming 8 rows.
volatile uint8_t KBD_COLFLAGS[IOSIZE];  // this is keeping the flag for scanning
volatile uint8_t KBD_BUFFER[ (IOSIZE * IOSIZE) ]; // keeps the status of the keyboard
volatile int KBD_SCANCOUNT;
volatile bool KBD_NOKEY = true;   // wether no keys are pressed

static inline IRAM_ATTR void cycle(uint32_t cycles) {
     // One cycle at 240 MHz is 4.16ns, 160 MHz is 6.25 ns
     for (int i=0; i < cycles; i++) __asm__ __volatile__("nop");
}

void typewrt_sleep_lock(void)
{
    __atomic_add_fetch(&typewrt_sleep_locks, 1, __ATOMIC_RELAXED);
}

void typewrt_sleep_unlock(void)
{
    uint32_t locks = __atomic_load_n(&typewrt_sleep_locks, __ATOMIC_RELAXED);

    while (locks) {
        if (__atomic_compare_exchange_n(&typewrt_sleep_locks, &locks,
                locks - 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return;
        }
    }
}

void typewrt_sleep_set_ui_wakeup_us(uint64_t delay_us)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)delay_us;

    __atomic_store_n(&typewrt_ui_wakeup_deadline_us, deadline,
        __ATOMIC_RELAXED);
}

void typewrt_sleep_clear_ui_wakeup(void)
{
    __atomic_store_n(&typewrt_ui_wakeup_deadline_us, 0, __ATOMIC_RELAXED);
}

static bool typewrt_sleep_is_locked(void)
{
    return __atomic_load_n(&typewrt_sleep_locks, __ATOMIC_RELAXED) != 0;
}

static bool typewrt_light_sleep_allowed(void)
{
    return !typewrt_sleep_is_locked() &&
        keyboard &&
        uxQueueMessagesWaiting(keyboard) == 0 &&
        KBD_NOKEY;
}

static void kbd_prepare_wakeup_rows(void)
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

static void typewrt_light_sleep_if_idle(void)
{
#if TYPEWRT_ENABLE_LIGHT_SLEEP
    esp_err_t ret;

    if (!typewrt_light_sleep_allowed()) {
        return;
    }
    typewrt_battery_monitor_check(false);
    if (!typewrt_light_sleep_allowed()) {
        return;
    }

    ret = esp_timer_stop(KBD_SCAN_TIMER);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to stop keyboard scan timer before sleep: %s",
            esp_err_to_name(ret));
        return;
    }

    if (!typewrt_light_sleep_allowed()) {
        KBD_SCANCOUNT = SCANTIMEOUT;
        ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));
        return;
    }

    typewrt_power_led_update();
    typewrt_usb_wakeup_prepare();
    splash_schedule_status_wakeup();
    typewrt_timer_wakeup_prepare();
    kbd_prepare_wakeup_rows();
    ret = esp_light_sleep_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "light sleep failed: %s", esp_err_to_name(ret));
    }
    typewrt_power_led_update();
    refreshSplashStatusClock();
    typewrt_battery_monitor_check(false);

    KBD_SCANCOUNT = SCANTIMEOUT;
    ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));
#endif
}


static IRAM_ATTR void kbd_scan(void* arg)
{
    ///kbd_t *kbd = (kbd_t *)arg;
    int col = -1;
    int row = -1;
    uint8_t key_event = 0;

    //ESP_LOGI(TAG, "-------------------  Matrix Scan Start ---------- ct: %d", KBD_SCANCOUNT);

    if (KBD_SCANCOUNT) {
      KBD_NOKEY = true;
      for (row = 0 ; row < IOSIZE ; row++) {
          GPIO.out_w1ts = (1UL << KBD_OE); // set OE high (active low)
	  cycle(16);
          GPIO.out_w1tc = KBD_IO_MASK;  
          GPIO.enable_w1ts = KBD_IO_MASK;  // set gpios to output
	  cycle(32);
	  // set all rows high but one 
          GPIO.out_w1ts = KBD_IO_MASK;  
          GPIO.out_w1tc = (1UL<< KBD_IO[row]);  
	  cycle(16);
          GPIO.out_w1ts = (1UL << KBD_LE); // set LE high to transfer to OUTPUT (D->Q)
	  cycle(32);
          GPIO.out_w1tc = (1UL << KBD_LE); // set LE low to latch
	  cycle(8);
          GPIO.out_w1ts = KBD_IO_MASK;  // set all pins to high (for cols)
	  cycle(16);
          GPIO.enable_w1tc = KBD_IO_MASK;  // set gpios to input (they are pulled up anyhow)
	  cycle(32);
          GPIO.out_w1tc = (1UL << KBD_OE); // set OE low to enable output
	  cycle(16);

          uint32_t cols_read = GPIO.in; // read cols 
          uint32_t cols_in = 0;
	  cols_read &= KBD_IO_MASK;
	  for (int k=0; k< IOSIZE; k++) {
	      cols_in |=  (((cols_read  >> KBD_IO[k]) & 1 ) << k);
	  }
          KBD_COLFLAGS[row] |= ( cols_in ^ (KBD_COLS[row]) );
          uint8_t scan = KBD_COLFLAGS[row];
          while (scan) {
              col = __builtin_ffs(scan) -1;
              KBD_BUFFER[(IOSIZE*row + col)] = ((KBD_BUFFER[(IOSIZE*row + col)]) << 1 ) | ((cols_in >> col) & 1);
              if (((KBD_BUFFER[(IOSIZE*row + col)]) == 0x00 ) && (KBD_COLS[row] & (1 << col))) {
                KBD_COLS[row] &= ~( 1  << col);
                //KBD_BUFFER[(IOSIZE*row + col)] = 0xFF; 
          	KBD_SCANCOUNT = SCANTIMEOUT;
                //ESP_LOGI(TAG, "Key  PRESSED  %d", (IOSIZE*row + col));
                //const char* kkk = "key event from ESP32s3\n";
                //uart_write_bytes(2, (const char *)kkk , strlen(kkk));
		key_event = KBDMAP[(IOSIZE*row + col)] | KEYDOWN_MASK;
                xQueueSend( keyboard , &key_event , 0);
          	KBD_COLFLAGS[row] &= (~(1 << col) & ((1<< IOSIZE) -1));
              }
              if (((KBD_BUFFER[(IOSIZE*row + col)]) == 0xFF ) && !(KBD_COLS[row] & (1 << col)) ) {
                KBD_COLS[row] |= ( 1  << col);
                //KBD_BUFFER[(IOSIZE*row + col)] = 0x00; 
          	KBD_SCANCOUNT = SCANTIMEOUT;
                //ESP_LOGI(TAG, "Key RELEASED  %d", (IOSIZE*row + col));
                //const char* kkk = "key event from ESP32s3\n";
                //uart_write_bytes(2, (const char *)kkk , strlen(kkk));
		key_event = KBDMAP[(IOSIZE*row + col)];
                xQueueSend( keyboard , &key_event , 0);
          	KBD_COLFLAGS[row] &= (~(1 << col) & ((1<< IOSIZE) -1));
              }
              scan &= (scan - 1);  // clears the lowest set bit
          }
          if ((~KBD_COLS[row]) & ((1UL << IOSIZE) -1)) KBD_NOKEY = false;
      }
      if (KBD_NOKEY) KBD_SCANCOUNT--;
    }
    else {
      // Sleep is managed from the keyboard read task, where queue idleness is visible.
      KBD_SCANCOUNT = SCANTIMEOUT;
    }
}

void kbd_start()
{

    // Create timer, used for stop scanning
    const esp_timer_create_args_t scan_timer_args = {
            .callback = &kbd_scan,
	    //.dispatch_method = ESP_TIMER_ISR,
            .name = "scaner"
    };
    esp_timer_create(&scan_timer_args, &KBD_SCAN_TIMER);

    for (int k=0; k < IOSIZE; k++) {
	    gpio_reset_pin(KBD_IO[k]);
	    KBD_COLS[k] = 0xFF; // all keys are released
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = KBD_IO_MASK,
        .intr_type = GPIO_INTR_DISABLE,  
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);
    
    // To enable later with
    // gpio_intr_enable(config->io_gpios[i]);

    gpio_config_t ol_config = {
        .pin_bit_mask = ((1ULL <<  KBD_OE) | (1ULL << KBD_LE)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&ol_config);
    gpio_sleep_sel_dis(KBD_OE);
    gpio_sleep_sel_dis(KBD_LE);

    //uart_config_t uart_conf = {
    //        .baud_rate = 115200,
    //        .data_bits = UART_DATA_8_BITS,
    //        .parity    = UART_PARITY_DISABLE,
    //        .stop_bits  = UART_STOP_BITS_1,
    //        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    //        .source_clk = UART_SCLK_DEFAULT,
    //};

    //ESP_ERROR_CHECK(uart_driver_install(2, 512 , 0, 0, NULL, ESP_INTR_FLAG_IRAM));
    //ESP_ERROR_CHECK(uart_param_config(2, &uart_conf));
    //ESP_ERROR_CHECK(uart_set_pin(2, 8, 9, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ////uart_write_bytes(2, (const char *) data, len);
    //const char* data = "Trying UART from ESP32s3\n";
    //uart_write_bytes(2, (const char *)data , strlen(data));


    /* Now, the interrupts are changed. They keyboard scanning process is not trigger by the edge interrupt. 
     * of a GPIO input. Rather, the board is woke up with the GPIO interrupt and continue the scanning process
     * without extra interrupts.
     */
#if TYPEWRT_ENABLE_LIGHT_SLEEP
    for (int i=0; i < IOSIZE; i++) {
	gpio_wakeup_enable(KBD_IO[i], GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
#endif

    KBD_SCANCOUNT = SCANTIMEOUT;
    ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));

}

static void splash_clock_start(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = splashClockTimerCallback,
        .name = "splash_clock",
        .skip_unhandled_events = true,
    };

    if (splash_clock_timer) {
        return;
    }
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &splash_clock_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(splash_clock_timer, 1000000));
}

bool typewrt_usb_power_present(void)
{
    return gpio_get_level(PIN_5V_EN) == 1;
}

static void typewrt_led_set(bool on)
{
    (void)gpio_hold_dis(PIN_LEDN);
    gpio_set_level(PIN_LEDN, on ? 0 : 1);
}

static void typewrt_power_led_update(void)
{
    if (__atomic_load_n(&typewrt_sd_write_locks, __ATOMIC_RELAXED) != 0 ||
            __atomic_load_n(&battery_led_active, __ATOMIC_RELAXED) ||
            __atomic_load_n(&boot_led_steps, __ATOMIC_RELAXED) != 0) {
        return;
    }
    typewrt_led_set(typewrt_usb_power_present());
    gpio_hold_en(PIN_LEDN);
}

static void typewrt_boot_led_cancel(void)
{
    __atomic_store_n(&boot_led_steps, 0, __ATOMIC_RELAXED);
    if (boot_led_timer) {
        esp_err_t ret = esp_timer_stop(boot_led_timer);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to stop boot LED timer: %s",
                esp_err_to_name(ret));
        }
    }
}

static void typewrt_boot_led_timer_callback(void *arg)
{
    uint32_t steps;

    (void)arg;
    if (__atomic_load_n(&typewrt_sd_write_locks, __ATOMIC_RELAXED) != 0) {
        __atomic_store_n(&boot_led_steps, 0, __ATOMIC_RELAXED);
        return;
    }
    steps = __atomic_load_n(&boot_led_steps, __ATOMIC_RELAXED);
    if (!steps) {
        return;
    }

    boot_led_on = !boot_led_on;
    typewrt_led_set(boot_led_on);
    steps = __atomic_sub_fetch(&boot_led_steps, 1, __ATOMIC_RELAXED);
    if (!steps) {
        typewrt_power_led_update();
        return;
    }
    esp_err_t ret = esp_timer_start_once(boot_led_timer,
        TYPEWRT_LED_BOOT_PULSE_US);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to continue boot LED timer: %s",
            esp_err_to_name(ret));
        __atomic_store_n(&boot_led_steps, 0, __ATOMIC_RELAXED);
        typewrt_power_led_update();
    }
}

static bool typewrt_boot_led_timer_prepare(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = typewrt_boot_led_timer_callback,
        .name = "boot_led",
    };
    esp_err_t ret;

    if (boot_led_timer) {
        return true;
    }
    ret = esp_timer_create(&timer_args, &boot_led_timer);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create boot LED timer: %s",
            esp_err_to_name(ret));
        return false;
    }
    return true;
}

static void typewrt_led_boot_blink_start(void)
{
    esp_err_t ret;

    if (!typewrt_boot_led_timer_prepare()) {
        return;
    }
    ret = esp_timer_stop(boot_led_timer);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to reset boot LED timer: %s",
            esp_err_to_name(ret));
    }

    boot_led_on = !typewrt_usb_power_present();
    __atomic_store_n(&boot_led_steps, TYPEWRT_LED_BOOT_BLINKS * 2 - 1,
        __ATOMIC_RELAXED);
    typewrt_led_set(boot_led_on);
    ret = esp_timer_start_once(boot_led_timer, TYPEWRT_LED_BOOT_PULSE_US);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start boot LED timer: %s",
            esp_err_to_name(ret));
        __atomic_store_n(&boot_led_steps, 0, __ATOMIC_RELAXED);
        typewrt_power_led_update();
    }
}

static void typewrt_battery_led_finish(void)
{
    bool locked = __atomic_exchange_n(&battery_led_locked, false,
        __ATOMIC_RELAXED);

    __atomic_store_n(&battery_led_active, false, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_steps, 0, __ATOMIC_RELAXED);
    typewrt_power_led_update();
    if (locked) {
        typewrt_sleep_unlock();
    }
}

static void typewrt_battery_led_cancel(void)
{
    if (battery_led_timer) {
        esp_err_t ret = esp_timer_stop(battery_led_timer);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to stop battery LED timer: %s",
                esp_err_to_name(ret));
        }
    }
    typewrt_battery_led_finish();
}

static void typewrt_battery_led_timer_callback(void *arg)
{
    uint32_t steps;

    (void)arg;
    if (__atomic_load_n(&typewrt_sd_write_locks, __ATOMIC_RELAXED) != 0) {
        typewrt_battery_led_finish();
        return;
    }
    steps = __atomic_load_n(&battery_led_steps, __ATOMIC_RELAXED);
    if (!__atomic_load_n(&battery_led_active, __ATOMIC_RELAXED) || !steps) {
        typewrt_battery_led_finish();
        return;
    }

    if (battery_led_off_us) {
        if (battery_led_on) {
            battery_led_on = false;
            typewrt_led_set(false);
            if (__atomic_sub_fetch(&battery_led_steps, 1, __ATOMIC_RELAXED) == 0) {
                typewrt_battery_led_finish();
                return;
            }
            esp_timer_start_once(battery_led_timer, battery_led_off_us);
            return;
        }
        battery_led_on = true;
        typewrt_led_set(true);
        esp_timer_start_once(battery_led_timer, battery_led_on_us);
        return;
    }

    if (__atomic_sub_fetch(&battery_led_steps, 1, __ATOMIC_RELAXED) == 0) {
        typewrt_battery_led_finish();
        return;
    }
    battery_led_on = !battery_led_on;
    typewrt_led_set(battery_led_on);
    esp_timer_start_once(battery_led_timer, battery_led_on_us);
}

static bool typewrt_battery_led_timer_prepare(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = typewrt_battery_led_timer_callback,
        .name = "bat_led",
    };
    esp_err_t ret;

    if (battery_led_timer) {
        return true;
    }
    ret = esp_timer_create(&timer_args, &battery_led_timer);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create battery LED timer: %s",
            esp_err_to_name(ret));
        return false;
    }
    return true;
}

static void typewrt_battery_led_start_low(void)
{
    if (!typewrt_battery_led_timer_prepare()) {
        return;
    }
    typewrt_battery_led_cancel();
    typewrt_boot_led_cancel();
    typewrt_sleep_lock();
    __atomic_store_n(&battery_led_locked, true, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_active, true, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_steps, TYPEWRT_BATTERY_WARN_LOW_PULSES,
        __ATOMIC_RELAXED);
    battery_led_on_us = TYPEWRT_BATTERY_WARN_PULSE_ON_US;
    battery_led_off_us = TYPEWRT_BATTERY_WARN_PULSE_GAP_US;
    battery_led_on = true;
    typewrt_led_set(true);
    esp_timer_start_once(battery_led_timer, battery_led_on_us);
}

static void typewrt_battery_led_start_critical(void)
{
    if (!typewrt_battery_led_timer_prepare()) {
        return;
    }
    typewrt_battery_led_cancel();
    typewrt_boot_led_cancel();
    typewrt_sleep_lock();
    __atomic_store_n(&battery_led_locked, true, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_active, true, __ATOMIC_RELAXED);
    __atomic_store_n(&battery_led_steps,
        TYPEWRT_BATTERY_WARN_CRIT_DURATION_US / TYPEWRT_BATTERY_WARN_CRIT_TOGGLE_US,
        __ATOMIC_RELAXED);
    battery_led_on_us = TYPEWRT_BATTERY_WARN_CRIT_TOGGLE_US;
    battery_led_off_us = 0;
    battery_led_on = true;
    typewrt_led_set(true);
    esp_timer_start_once(battery_led_timer, battery_led_on_us);
}

static void typewrt_sd_write_led_timer_callback(void *arg)
{
    (void)arg;

    if (__atomic_load_n(&typewrt_sd_write_locks, __ATOMIC_RELAXED) == 0) {
        return;
    }
    sd_write_led_on = !sd_write_led_on;
    typewrt_led_set(sd_write_led_on);
}

static bool typewrt_sd_write_led_timer_prepare(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = typewrt_sd_write_led_timer_callback,
        .name = "sd_led",
    };
    esp_err_t ret;

    if (sd_write_led_timer) {
        return true;
    }
    ret = esp_timer_create(&timer_args, &sd_write_led_timer);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create SD write LED timer: %s",
            esp_err_to_name(ret));
        return false;
    }
    return true;
}

void typewrt_sd_write_begin(void)
{
    uint32_t locks;
    esp_err_t ret;

    typewrt_sleep_lock();
    typewrt_boot_led_cancel();
    typewrt_battery_led_cancel();
    locks = __atomic_add_fetch(&typewrt_sd_write_locks, 1, __ATOMIC_RELAXED);
    if (locks != 1) {
        return;
    }
    if (!typewrt_sd_write_led_timer_prepare()) {
        return;
    }

    sd_write_led_on = true;
    typewrt_led_set(true);
    ret = esp_timer_stop(sd_write_led_timer);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Failed to reset SD write LED timer: %s",
            esp_err_to_name(ret));
    }
    ret = esp_timer_start_periodic(sd_write_led_timer,
        TYPEWRT_LED_SD_BLINK_PERIOD_US);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start SD write LED timer: %s",
            esp_err_to_name(ret));
    }
}

void typewrt_sd_write_end(void)
{
    uint32_t locks = __atomic_load_n(&typewrt_sd_write_locks,
        __ATOMIC_RELAXED);

    while (locks) {
        if (__atomic_compare_exchange_n(&typewrt_sd_write_locks, &locks,
                locks - 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            if (locks == 1) {
                if (sd_write_led_timer) {
                    esp_err_t ret = esp_timer_stop(sd_write_led_timer);
                    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                        ESP_LOGW(TAG, "Failed to stop SD write LED timer: %s",
                            esp_err_to_name(ret));
                    }
                }
                typewrt_power_led_update();
            }
            typewrt_sleep_unlock();
            return;
        }
    }
}

static uint64_t typewrt_battery_check_interval_us(uint32_t soc_tenths)
{
    if (soc_tenths >= 500) {
        return TYPEWRT_BATTERY_CHECK_HIGH_US;
    }
    if (soc_tenths >= TYPEWRT_BATTERY_WARN_LOW_SOC_TENTHS) {
        return TYPEWRT_BATTERY_CHECK_MED_US;
    }
    return TYPEWRT_BATTERY_CHECK_LOW_US;
}

static void typewrt_battery_schedule_next_check(int64_t now, uint64_t delay_us)
{
    battery_next_check_us = now + (int64_t)delay_us;
}

static void typewrt_battery_monitor_check(bool force)
{
    typewrt_battery_sample_t sample;
    int64_t now = esp_timer_get_time();
    uint8_t warning_level = 0;
    uint64_t warning_repeat_us = 0;
    bool discharging;
    esp_err_t ret;

    if (!force && battery_next_check_us && now < battery_next_check_us) {
        return;
    }
    typewrt_sleep_lock();
    ret = typewrt_battery_read_sample(&sample);
    typewrt_sleep_unlock();
    if (ret != ESP_OK || sample.absent) {
        battery_warning_level = 0;
        battery_next_warning_us = 0;
        typewrt_battery_schedule_next_check(now, TYPEWRT_BATTERY_CHECK_HIGH_US);
        return;
    }

    typewrt_battery_schedule_next_check(now,
        typewrt_battery_check_interval_us(sample.soc_tenths));
    discharging = !typewrt_usb_power_present() && sample.rate_tenths <= 0;
    if (!discharging) {
        battery_warning_level = 0;
        battery_next_warning_us = 0;
        return;
    }

    if (sample.soc_tenths < TYPEWRT_BATTERY_WARN_CRIT_SOC_TENTHS) {
        warning_level = 2;
        warning_repeat_us = TYPEWRT_BATTERY_WARN_CRIT_REPEAT_US;
    } else if (sample.soc_tenths < TYPEWRT_BATTERY_WARN_LOW_SOC_TENTHS) {
        warning_level = 1;
        warning_repeat_us = TYPEWRT_BATTERY_WARN_LOW_REPEAT_US;
    } else {
        battery_warning_level = 0;
        battery_next_warning_us = 0;
        return;
    }

    if (warning_level > battery_warning_level || !battery_next_warning_us ||
            now >= battery_next_warning_us) {
        if (warning_level == 2) {
            typewrt_battery_led_start_critical();
        } else {
            typewrt_battery_led_start_low();
        }
        battery_next_warning_us = now + (int64_t)warning_repeat_us;
    }
    battery_warning_level = warning_level;
}

static void typewrt_timer_wakeup_prepare(void)
{
    int64_t now;
    uint64_t delay_us = 0;
    int64_t ui_deadline;
    bool have_timer = false;

    now = esp_timer_get_time();
    if (battery_gauge_available && battery_i2c_dev && battery_next_check_us) {
        delay_us = battery_next_check_us > now ?
            (uint64_t)(battery_next_check_us - now) : 1;
        have_timer = true;
    }
    ui_deadline = __atomic_load_n(&typewrt_ui_wakeup_deadline_us,
        __ATOMIC_RELAXED);
    if (ui_deadline > 0) {
        uint64_t ui_delay_us = ui_deadline > now ?
            (uint64_t)(ui_deadline - now) : 1;
        if (!have_timer || ui_delay_us < delay_us) {
            delay_us = ui_delay_us;
        }
        have_timer = true;
    }
    if (!have_timer) {
        (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        return;
    }
    esp_sleep_enable_timer_wakeup(delay_us);
}

static void typewrt_battery_monitor_start(void)
{
    battery_next_check_us = 0;
    battery_next_warning_us = 0;
    battery_warning_level = 0;
    typewrt_battery_monitor_check(true);
}

static void typewrt_reset_button_enable(bool enabled)
{
    (void)gpio_hold_dis(PIN_RST_EN);
    gpio_set_level(PIN_RST_EN, enabled ? 0 : 1);
    gpio_hold_en(PIN_RST_EN);
}

static void typewrt_usb_wakeup_prepare(void)
{
    (void)gpio_wakeup_disable(PIN_5V_EN);
    gpio_wakeup_enable(PIN_5V_EN,
        typewrt_usb_power_present() ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
}

static void typewrt_power_domain_pins_high_z(void)
{
    uint64_t pin_mask = (uint64_t)KBD_IO_MASK |
        (1ULL << KBD_OE) |
        (1ULL << KBD_LE) |
        (1ULL << PIN_NUM_CS) |
        (1ULL << PIN_NUM_MOSI) |
        (1ULL << PIN_NUM_MISO) |
        (1ULL << PIN_NUM_CLK);
    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    gpio_config(&io_conf);
}

void typewrt_power_off(void)
{
    typewrt_sleep_lock();
    if (splash_clock_timer) {
        (void)esp_timer_stop(splash_clock_timer);
    }
    if (KBD_SCAN_TIMER) {
        (void)esp_timer_stop(KBD_SCAN_TIMER);
    }

    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    (void)gpio_wakeup_disable(PIN_5V_EN);
    for (int i = 0; i < IOSIZE; i++) {
        (void)gpio_wakeup_disable(KBD_IO[i]);
    }

    (void)gpio_hold_dis(PIN_LEDN);
    gpio_set_level(PIN_LEDN, 1);
    gpio_hold_en(PIN_LEDN);

    typewrt_reset_button_enable(true);
    typewrt_power_domain_pins_high_z();

    (void)gpio_hold_dis(PIN_LDO2_EN);
    gpio_set_level(PIN_LDO2_EN, 0);
    gpio_hold_en(PIN_LDO2_EN);
    gpio_deep_sleep_hold_en();

    esp_deep_sleep_start();
}

static void power_mng_init(void)
{
    gpio_deep_sleep_hold_dis();
    (void)gpio_hold_dis(PIN_LDO2_EN);
    (void)gpio_hold_dis(PIN_LEDN);
    (void)gpio_hold_dis(PIN_RST_EN);

    gpio_config_t power_conf = {
        .pin_bit_mask = (1ULL << PIN_LDO2_EN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&power_conf);
    gpio_set_level(PIN_LDO2_EN, 1);
    gpio_hold_en(PIN_LDO2_EN);

    gpio_config_t usb_conf = {
        .pin_bit_mask = (1ULL << PIN_5V_EN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&usb_conf);

    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_LEDN ) | (1ULL << PIN_RST_EN),
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(PIN_LEDN, 1);
    typewrt_power_led_update();
    typewrt_reset_button_enable(true);
    typewrt_led_boot_blink_start();
}


void app_main(void)
{


    // Setup the light sleep mode
    //esp_sleep_enable_gpio_wakeup(); 
    //esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
    //esp_wifi_stop();
    //esp_bt_controller_disable();
    power_mng_init();

    esp_rom_delay_us(500);
    //xTaskCreate(vTaskStandBy, "standby", 2048, NULL, 5, NULL);
    // Keyboard start to work

    rtc_init();
    battery_init();
    sdcard_spi_pins_prepare();
    displayInit();
    clearDisplay();
    sdcard_init();

    // Start the keyboard queue before Nextvi begins reading input.
    keyboard = xQueueCreateStatic( KBD_EVENT_QUEUE_LENGTH, // The number of items the queue can hold.
                         KBD_EVENT_SIZE,      // The size of each item in the queue
                         &( kbd_QueueStorage[ 0 ] ), // The buffer that will hold the items in the queue.
                         &kbd_StaticQueue ); // The buffer that will hold the queue structure.
    xTaskCreate(vNextviTask, "nextvi", 16384, NULL, 5, NULL);

    kbd_start();
    splash_clock_start();
    ESP_LOGI(TAG, "Keyboard started");
}
