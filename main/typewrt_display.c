#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "typewrt_board.h"
#include "typewrt_display.h"
#include "typewrt_fontmap.h"
#include "typewrt_power.h"
#include "typewrt_splash.h"
#include "zap-vga16-raw-neg.h"

#define PSF_GLYPH_SIZE TYPEWRT_FONT_HEIGHT

#define SHARPMEM_BIT_WRITECMD (0x01)
#define SHARPMEM_BIT_CLEAR (0x04)

#define ESP_HOST TYPEWRT_SPI_HOST
#define PIN_NUM_MOSI TYPEWRT_PIN_SPI_MOSI
#define PIN_NUM_MISO TYPEWRT_PIN_SPI_MISO
#define PIN_NUM_CLK  TYPEWRT_PIN_SPI_CLK
#define PIN_NUM_CS   TYPEWRT_PIN_DISPLAY_CS

#define PXWIDTH TYPEWRT_DISPLAY_WIDTH
#define PXHEIGHT TYPEWRT_DISPLAY_HEIGHT

#define SHARPMEM_BYTES_PER_LINE (PXWIDTH / 8)
#define SHARPMEM_BUFFER_BYTES ((PXWIDTH * PXHEIGHT) / 8)

static spi_device_handle_t spi;
static uint8_t *sharpmem_buffer;
static char display_shadow[NEXTVI_DISPLAY_ROWS + 1][NEXTVI_DISPLAY_COLS + 1];
static bool splash_active = true;
static bool splash_drawn;
static bool splash_disable_pending;
static bool display_cursor_drawn;
static int display_cursor_row = -1;
static int display_cursor_col = -1;
static esp_timer_handle_t splash_clock_timer;
static char splash_clock_last[24];
static char splash_status_last[NEXTVI_DISPLAY_COLS + 1];
static StaticSemaphore_t display_lock_storage;
static SemaphoreHandle_t display_lock;

void typewrt_display_init(void)
{
    // VCOM inversion is handled externally by the display hardware.
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

void typewrt_display_clear(void) {
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

static void __attribute__((unused)) refreshDisplay(void) {
  spi_transaction_t t;

  sharpmem_start_write(&t, 6);
  for (uint16_t physical_line = 0; physical_line < PXHEIGHT; physical_line++) {
    sharpmem_send_physical_line(&t, physical_line);
  }
  sharpmem_finish_write(&t, 2);
  }

static void updateRow(uint8_t row) {
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
    return fontmap[FONTMAP_REPLACEMENT_INDEX];
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
    return fontmap[FONTMAP_REPLACEMENT_INDEX];
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

static void splashCompactTimestamp(const char *timestamp, char *out, size_t out_len)
{
    char day[4], month[4], time_part[8];

    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (sscanf(timestamp, "%3s %3s %*5s %7s", day, month, time_part) == 3) {
        snprintf(out, out_len, "%s %s %s", day, month, time_part);
        return;
    }
    snprintf(out, out_len, "%s", timestamp);
}

static void splashStatusLine(char line[NEXTVI_DISPLAY_COLS + 1])
{
    char timestamp[24];
    char timestamp_short[24];
    char battery[16];
    char right[64];
    size_t ts_len;
    size_t right_len;

    memcpy(line, display_shadow[NEXTVI_DISPLAY_ROWS], NEXTVI_DISPLAY_COLS);
    line[NEXTVI_DISPLAY_COLS] = '\0';
    if (!typewrt_rtc_get_datetime(timestamp, sizeof(timestamp))) {
        return;
    }
    splashCompactTimestamp(timestamp, timestamp_short, sizeof(timestamp_short));
    splashBatteryStatus(battery, sizeof(battery));
    snprintf(right, sizeof(right), "%s %s", battery, timestamp_short);
    right_len = strlen(right);
    ts_len = strlen(timestamp_short);
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

void typewrt_display_splash_clock_start(void)
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


void typewrt_display_prepare_sleep(void)
{
    splash_schedule_status_wakeup();
}

void typewrt_display_after_sleep(void)
{
    refreshSplashStatusClock();
}

void typewrt_display_stop_for_poweroff(void)
{
    if (splash_clock_timer) {
        (void)esp_timer_stop(splash_clock_timer);
    }
}

void typewrt_display_power_pins_high_z(void)
{
    uint64_t pin_mask = (1ULL << PIN_NUM_CS) |
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
