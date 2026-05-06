#include "typewrt_display.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "zap-vga16-raw-neg.h"

#define SHARPMEM_BIT_WRITECMD (0x01)
#define SHARPMEM_BIT_CLEAR (0x04)

#define ESP_HOST SPI2_HOST
#define PIN_NUM_MOSI 35
#define PIN_NUM_CLK 36
#define PIN_NUM_CS 38

static spi_device_handle_t s_spi;
static DMA_ATTR uint8_t *s_sharpmem_buffer = NULL;

static const uint8_t set_mask[] = {1, 2, 4, 8, 16, 32, 64, 128};
static const uint8_t clear_mask[] = {
    (uint8_t)~1, (uint8_t)~2, (uint8_t)~4, (uint8_t)~8,
    (uint8_t)~16, (uint8_t)~32, (uint8_t)~64, (uint8_t)~128,
};

void typewrt_display_init(void)
{
    esp_err_t ret;

    s_sharpmem_buffer = (uint8_t *)malloc((TYPEWRT_DISPLAY_WIDTH * TYPEWRT_DISPLAY_HEIGHT) / 8);
    if (!s_sharpmem_buffer) {
        printf("Error: sharpmem_buffer was NOT allocated\n\n");
        return;
    }

    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);

    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512 * 8,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .flags = (SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_3WIRE),
        .queue_size = 7,
    };

    ret = spi_bus_initialize(ESP_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(ESP_HOST, &devcfg, &s_spi);
    ESP_ERROR_CHECK(ret);
    printf("SPI initialized. MOSI:%d CLK:%d CS:%d\n", PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
    gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);

    vTaskDelay(100 / portTICK_PERIOD_MS);
}

void typewrt_display_set_pixel(int16_t x, int16_t y, uint16_t color)
{
    if (color) {
        s_sharpmem_buffer[(y * TYPEWRT_DISPLAY_WIDTH + x) / 8] |= set_mask[x & 7];
    } else {
        s_sharpmem_buffer[(y * TYPEWRT_DISPLAY_WIDTH + x) / 8] &= clear_mask[x & 7];
    }
}

uint8_t typewrt_display_get_pixel(uint16_t x, uint16_t y)
{
    if ((x >= TYPEWRT_DISPLAY_WIDTH) || (y >= TYPEWRT_DISPLAY_HEIGHT)) {
        return 0;
    }

    return s_sharpmem_buffer[(y * TYPEWRT_DISPLAY_WIDTH + x) / 8] & set_mask[x & 7] ? 1 : 0;
}

void typewrt_display_clear(void)
{
    memset(s_sharpmem_buffer, 0xff, (TYPEWRT_DISPLAY_WIDTH * TYPEWRT_DISPLAY_HEIGHT) / 8);
    gpio_set_level((gpio_num_t)PIN_NUM_CS, 1);
    esp_rom_delay_us(6);

    uint8_t clear_data[2] = {(uint8_t)SHARPMEM_BIT_CLEAR, 0x00};
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = sizeof(clear_data) * 8;
    t.tx_buffer = clear_data;

    esp_err_t ret = spi_device_polling_transmit(s_spi, &t);
    gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);
    esp_rom_delay_us(2);
    assert(ret == ESP_OK);
}

void typewrt_display_refresh(void)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    gpio_set_level((gpio_num_t)PIN_NUM_CS, 1);
    esp_rom_delay_us(6);

    uint8_t write_data[1] = {(uint8_t)SHARPMEM_BIT_WRITECMD};
    t.length = sizeof(write_data) * 8;
    t.tx_buffer = write_data;
    ret = spi_device_transmit(s_spi, &t);

    uint8_t bytes_per_line = TYPEWRT_DISPLAY_WIDTH / 8;
    uint16_t totalbytes = (TYPEWRT_DISPLAY_WIDTH * TYPEWRT_DISPLAY_HEIGHT) / 8;

    for (uint16_t i = 0; i < totalbytes; i += bytes_per_line) {
        uint8_t line[bytes_per_line + 2];
        line[0] = ((i + 1) / (TYPEWRT_DISPLAY_WIDTH / 8)) + 1;
        memcpy(line + 1, s_sharpmem_buffer + i, bytes_per_line);
        line[bytes_per_line + 1] = 0x00;

        t.length = (bytes_per_line + 2) * 8;
        t.tx_buffer = line;
        ret = spi_device_transmit(s_spi, &t);
        assert(ret == ESP_OK);
    }

    int last_line[1] = {0x00};
    t.length = 8;
    t.tx_buffer = last_line;
    ret = spi_device_transmit(s_spi, &t);
    gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);
    esp_rom_delay_us(2);

    assert(ret == ESP_OK);
}

void typewrt_display_update_text_row(uint8_t row)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    gpio_set_level((gpio_num_t)PIN_NUM_CS, 1);
    esp_rom_delay_us(1);

    uint8_t write_data[1] = {(uint8_t)SHARPMEM_BIT_WRITECMD};
    t.length = sizeof(write_data) * 8;
    t.tx_buffer = write_data;
    ret = spi_device_transmit(s_spi, &t);

    uint8_t bytes_per_line = TYPEWRT_DISPLAY_WIDTH / 8;
    uint8_t line[bytes_per_line + 2];
    line[0] = (uint8_t)(TYPEWRT_DISPLAY_GLYPH_HEIGHT * row);

    for (uint8_t i = 0; i < TYPEWRT_DISPLAY_GLYPH_HEIGHT; i++) {
        memcpy(line + 1, s_sharpmem_buffer + line[0] * bytes_per_line, bytes_per_line);
        line[0]++;
        line[bytes_per_line + 1] = 0x00;

        t.length = (bytes_per_line + 2) * 8;
        t.tx_buffer = line;
        ret = spi_device_transmit(s_spi, &t);
        assert(ret == ESP_OK);
    }

    int last_line[1] = {0x00};
    t.length = 8;
    t.tx_buffer = last_line;
    ret = spi_device_transmit(s_spi, &t);
    gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);
    esp_rom_delay_us(1);

    assert(ret == ESP_OK);
}

void typewrt_display_draw_glyph(uint8_t glyph_index, uint8_t column, uint8_t row)
{
    for (int m = 0; m < TYPEWRT_DISPLAY_GLYPH_HEIGHT; m++) {
        s_sharpmem_buffer[(((row * TYPEWRT_DISPLAY_GLYPH_HEIGHT + m) * TYPEWRT_DISPLAY_WIDTH) + 8 * column) / 8] =
            zap_vga16_psf[glyph_index * TYPEWRT_DISPLAY_GLYPH_HEIGHT + m];
    }

    typewrt_display_update_text_row(row);
}

void typewrt_display_clear_buffer(void)
{
    memset(s_sharpmem_buffer, 0xff, (TYPEWRT_DISPLAY_WIDTH * TYPEWRT_DISPLAY_HEIGHT) / 8);
}
