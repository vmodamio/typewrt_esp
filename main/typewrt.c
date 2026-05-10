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



#define SHARPMEM_BYTES_PER_LINE (PXWIDTH / 8)
#define SHARPMEM_BUFFER_BYTES ((PXWIDTH * PXHEIGHT) / 8)

#define KEY(r, c) ((r << 3) + c)
#define CUR( x, y ) (x + y*PXWIDTH/8)  

#define KBD_EVENT_QUEUE_LENGTH 32
#define KBD_EVENT_SIZE sizeof( uint8_t )
#define TYPEWRT_ENABLE_LIGHT_SLEEP 0
#define TYPEWRT_REFRESH_FULL_DISPLAY 0
#define TYPEWRT_SD_MOUNT_POINT "/sdcard"
#define TYPEWRT_RTC_I2C_PORT I2C_NUM_0
#define TYPEWRT_RTC_I2C_FREQ_HZ 100000
#define PCF8523_I2C_ADDRESS 0x68
#define PCF8523_CONTROL_1_REG 0x00
#define PCF8523_CONTROL_1_STOP BIT(5)
#define PCF8523_TIME_REG 0x03
#define PCF8523_SECONDS_OS BIT(7)

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
DMA_ATTR uint8_t *sharpmem_buffer = NULL;
static char display_shadow[NEXTVI_DISPLAY_ROWS + 1][NEXTVI_DISPLAY_COLS + 1];
static bool splash_active = true;
static bool splash_drawn;
static bool splash_disable_pending;
static bool display_cursor_drawn;
static int display_cursor_row = -1;
static int display_cursor_col = -1;
static esp_timer_handle_t splash_clock_timer;
static char splash_clock_last[24];
bool typewrt_rtc_get_datetime(char *out, size_t out_len);

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

void clearDisplayBuffer() {
  memset(sharpmem_buffer, 0xFF, SHARPMEM_BUFFER_BYTES);
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

static void markPhysicalRowRedrawn(int row)
{
    if (display_cursor_drawn && display_cursor_row == row) {
        display_cursor_drawn = false;
    }
}

static void copyDisplayShadow(int row, const char *text, int cols)
{
    if (row < 0 || row > NEXTVI_DISPLAY_ROWS) {
        return;
    }
    cols = cols > NEXTVI_DISPLAY_COLS ? NEXTVI_DISPLAY_COLS : cols;
    for (int col = 0; col < NEXTVI_DISPLAY_COLS; col++) {
        display_shadow[row][col] = col < cols && text[col] ? text[col] : ' ';
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

static void renderTextRow(int physical_row, const char *text)
{
    if (!sharpmem_buffer || physical_row < 0 || physical_row > NEXTVI_DISPLAY_ROWS) {
        return;
    }

    for (int col = 0; col < NEXTVI_DISPLAY_COLS; col++) {
        unsigned char ch = (unsigned char)text[col];
        displayGlyph(col, physical_row, ch ? ch : ' ');
    }
    markPhysicalRowRedrawn(physical_row);
#if TYPEWRT_REFRESH_FULL_DISPLAY
    refreshDisplay();
#else
    updateRow((uint8_t)physical_row);
#endif
}

static void renderBlankTextRow(int physical_row)
{
    static const char blank[NEXTVI_DISPLAY_COLS + 1] = "                                        ";

    renderTextRow(physical_row, blank);
}

static void splashStatusLine(char line[NEXTVI_DISPLAY_COLS + 1])
{
    char timestamp[24];
    size_t ts_len;

    memcpy(line, display_shadow[NEXTVI_DISPLAY_ROWS], NEXTVI_DISPLAY_COLS);
    line[NEXTVI_DISPLAY_COLS] = '\0';
    if (!typewrt_rtc_get_datetime(timestamp, sizeof(timestamp))) {
        return;
    }
    ts_len = strlen(timestamp);
    if (ts_len > NEXTVI_DISPLAY_COLS) {
        return;
    }
    memcpy(line + NEXTVI_DISPLAY_COLS - ts_len, timestamp, ts_len);
}

static void refreshSplashStatusClock(void)
{
    char timestamp[sizeof(splash_clock_last)];
    char status_line[NEXTVI_DISPLAY_COLS + 1];

    if (!splash_active || !splash_drawn) {
        return;
    }
    if (!typewrt_rtc_get_datetime(timestamp, sizeof(timestamp))) {
        return;
    }
    if (!strcmp(timestamp, splash_clock_last)) {
        return;
    }
    strncpy(splash_clock_last, timestamp, sizeof(splash_clock_last) - 1);
    splash_clock_last[sizeof(splash_clock_last) - 1] = '\0';
    splashStatusLine(status_line);
    renderTextRow(NEXTVI_DISPLAY_ROWS, status_line);
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
    renderTextRow(NEXTVI_DISPLAY_ROWS - 1, display_shadow[0]);
    splashStatusLine(status_line);
    renderTextRow(NEXTVI_DISPLAY_ROWS, status_line);
    typewrt_rtc_get_datetime(splash_clock_last, sizeof(splash_clock_last));
    splash_drawn = true;
}

static void disableSplash(void)
{
    if (!splash_active) {
        return;
    }
    if (splash_clock_timer) {
        (void)esp_timer_stop(splash_clock_timer);
    }
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
        renderTextRow(row, display_shadow[row]);
    }
}

void nextvi_display_refresh_line(int row, const char *text, int cols)
{
    if (!sharpmem_buffer || row < 0 || row > NEXTVI_DISPLAY_ROWS) {
        return;
    }

    copyDisplayShadow(row, text, cols);
    if (splash_active && splash_disable_pending && row == 0 &&
            displayShadowRowHasVisibleText(row)) {
        disableSplash();
        return;
    }
    if (splash_active) {
        drawSplashLayout();
        if (row == 0) {
            renderTextRow(NEXTVI_DISPLAY_ROWS - 1, display_shadow[0]);
        } else if (row == NEXTVI_DISPLAY_ROWS) {
            char status_line[NEXTVI_DISPLAY_COLS + 1];
            splashStatusLine(status_line);
            renderTextRow(row, status_line);
        }
        return;
    }

    renderTextRow(row, display_shadow[row]);
}

static int cursorCellValid(int row, int col)
{
    return sharpmem_buffer && row >= 0 && row <= NEXTVI_DISPLAY_ROWS &&
        col >= 0 && col < NEXTVI_DISPLAY_COLS;
}

static void invertCursorCell(int row, int col)
{
    for (int m = 0; m < PSF_GLYPH_SIZE; m++) {
        sharpmem_buffer[((row * PSF_GLYPH_SIZE + m) * PXWIDTH + 8 * col) / 8] ^= 0xff;
    }
}

void nextvi_display_refresh_cursor(int row, int col, int on)
{
    if (!on) {
        if (display_cursor_drawn) {
            invertCursorCell(display_cursor_row, display_cursor_col);
            updateRow((uint8_t)display_cursor_row);
            display_cursor_drawn = false;
        }
        return;
    }

    row = mapSplashRow(row);
    if (!cursorCellValid(row, col)) {
        return;
    }
    invertCursorCell(row, col);
    updateRow((uint8_t)row);
    display_cursor_row = row;
    display_cursor_col = col;
    display_cursor_drawn = true;
}

void nextvi_display_move_cursor(int old_row, int old_col, int new_row, int new_col, int on)
{
    bool old_valid = display_cursor_drawn &&
        cursorCellValid(display_cursor_row, display_cursor_col);
    int new_physical_row = mapSplashRow(new_row);
    bool new_valid = on && cursorCellValid(new_physical_row, new_col);

    if (!old_valid && !new_valid) {
        return;
    }
    if (old_valid && new_valid && display_cursor_row == new_physical_row &&
            display_cursor_col == new_col) {
        return;
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
    (void)old_row;
    (void)old_col;
}

void nextvi_display_note_insert(void)
{
    if (displayShadowRowHasVisibleText(0)) {
        disableSplash();
    } else {
        splash_disable_pending = true;
    }
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

int nextvi_keyboard_read(unsigned char *event)
{
    if (!keyboard) {
        return 0;
    }
    if (xQueueReceive(keyboard, event, portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    *event = nextvi_keyboard_translate_event(*event);
    return 1;
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
    else {  // GO TO STANDBY MODE
#if TYPEWRT_ENABLE_LIGHT_SLEEP
      ESP_ERROR_CHECK(esp_timer_stop(KBD_SCAN_TIMER)); // no more scanning
      //ESP_LOGI(TAG, "timer stopped, trying to enter light sleep...");
      GPIO.out_w1ts = (1UL << KBD_OE); // set OE high (active low)
      cycle(16);
      GPIO.enable_w1ts = KBD_IO_MASK;  // set gpios to output
      cycle(32);
      GPIO.out_w1tc = KBD_IO_MASK;  // set all pins to low (for ROWS)
      GPIO.out_w1ts = (1UL << KBD_LE); // set LE high to transfer to OUTPUT (D->Q)
      cycle(4);
      GPIO.out_w1tc = (1UL << KBD_LE); // set LE low to latch
      cycle(32);
      //GPIO.out_w1ts = mkbd->io_mask;  // set all pins to high (for cols)
      cycle(4);
      GPIO.enable_w1tc = KBD_IO_MASK;  // set gpios to input (they are pulled up anyhow)
      cycle(32);
      GPIO.out_w1tc = (1UL << KBD_OE); // set OE low to enable output
      cycle(16);

      ESP_ERROR_CHECK(esp_light_sleep_start());

      KBD_SCANCOUNT = SCANTIMEOUT;
      ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));

      //ESP_LOGI(TAG, "woken up...");
#else
      KBD_SCANCOUNT = SCANTIMEOUT;
#endif
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
    };

    if (splash_clock_timer) {
        return;
    }
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &splash_clock_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(splash_clock_timer, 1000000));
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
