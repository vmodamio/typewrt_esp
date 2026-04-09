#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_private/gpio.h"
#include "zap-vga16-raw-neg.h"
#include "keyboard_input.h"
#include "sharp_display.h"

DMA_ATTR uint8_t *sharpmem_buffer = NULL;

void displayInit(void)
{
    // Start the VCOM toggling task
    //xTaskCreate(&vcom_toggle_task, "vcom", 2048, NULL, 5, NULL);

    esp_err_t ret;
    /* Allocate pixel buffer for SHARP DISP*/
    sharpmem_buffer = (uint8_t *)malloc((PXWIDTH * PXHEIGHT) / 8);
    if (!sharpmem_buffer) {
      printf("Error: sharpmem_buffer was NOT allocated\n\n");
      return;
    }

    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);                   // Setting the CS' pin to work in OUTPUT mode

    spi_bus_config_t buscfg = {                                         // Provide details to the SPI_bus_sturcture of pins and maximum data size
        .miso_io_num = -1,
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
    printf("SPI initialized. MOSI:%d CLK:%d CS:%d\n", PIN_NUM_MOSI, PIN_NUM_CLK, PIN_NUM_CS);
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

void clearDisplay() {
  memset(sharpmem_buffer, 0xff, (PXWIDTH * PXHEIGHT) / 8);
  gpio_set_level((gpio_num_t)PIN_NUM_CS, 1);
  esp_rom_delay_us(6);
  uint8_t clear_data[2] = {(uint8_t)(SHARPMEM_BIT_CLEAR), 0x00};
  esp_err_t ret;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));        //Zero out the transaction
  t.length = sizeof(clear_data)*8; //Each data byte is 8 bits Einstein
  t.tx_buffer = clear_data;
  ret = spi_device_polling_transmit(spi, &t); // spi_device_polling_transmit
  gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);
  esp_rom_delay_us(2);
  //printf("clearDisplay b1:%02x 2:%02x lenght:%d\n\n", clear_data[0], clear_data[1], t.length);
  assert(ret==ESP_OK);
}

void refreshDisplay(void) {
  uint16_t i, currentline;

  esp_err_t ret;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));       //Zero out the transaction

  gpio_set_level((gpio_num_t)PIN_NUM_CS, 1);
  esp_rom_delay_us(6);
  uint8_t write_data[1] = {(uint8_t)SHARPMEM_BIT_WRITECMD};
  t.length = sizeof(write_data)*8;                  //Each data byte is 8 bits
  t.tx_buffer = write_data;
  ret = spi_device_transmit(spi, &t);

  uint8_t bytes_per_line = PXWIDTH / 8;
  uint16_t totalbytes = (PXWIDTH * PXHEIGHT) / 8;

  for (i = 0; i < totalbytes; i += bytes_per_line) {
    uint8_t line[bytes_per_line + 2];

    // Send address byte
    currentline = ((i + 1) / (PXWIDTH / 8)) + 1;
    line[0] = currentline;
    // copy over this line
    memcpy(line + 1, sharpmem_buffer + i, bytes_per_line);
    // Send end of line
    line[bytes_per_line + 1] = 0x00;

    t.length = (bytes_per_line+2) *8; // bytes_per_line+2
    t.tx_buffer = line;
    ret = spi_device_transmit(spi, &t);
    assert(ret==ESP_OK);
  }
  // Send another trailing 8 bits for the last line
  int last_line[1] = {0x00};
  t.length = 8;

  t.tx_buffer = last_line;
  ret = spi_device_transmit(spi, &t); // spi_device_polling_transmit
  gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);
  esp_rom_delay_us(2);

  assert(ret==ESP_OK);
  }

void updateRow(uint8_t row) {
  uint8_t i;

  esp_err_t ret;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));       //Zero out the transaction

  gpio_set_level((gpio_num_t)PIN_NUM_CS, 1);
  esp_rom_delay_us(1);
  uint8_t write_data[1] = {(uint8_t)SHARPMEM_BIT_WRITECMD};
  t.length = sizeof(write_data)*8;                  //Each data byte is 8 bits
  t.tx_buffer = write_data;
  ret = spi_device_transmit(spi, &t);

  uint8_t bytes_per_line = PXWIDTH / 8;
  uint8_t line[bytes_per_line + 2];
  line[0] = (uint8_t)(PSF_GLYPH_SIZE * row);

  for (i = 0; i < PSF_GLYPH_SIZE; i++) {
    memcpy(line + 1, sharpmem_buffer + line[0]*bytes_per_line, bytes_per_line);
    line[0]++;
    line[bytes_per_line + 1] = 0x00;

    t.length = (bytes_per_line+2) *8; // bytes_per_line+2
    t.tx_buffer = line;
    ret = spi_device_transmit(spi, &t);
    assert(ret==ESP_OK);
  }
  // Send another trailing 8 bits for the last line
  int last_line[1] = {0x00};
  t.length = 8;

  t.tx_buffer = last_line;
  ret = spi_device_transmit(spi, &t); // spi_device_polling_transmit
  gpio_set_level((gpio_num_t)PIN_NUM_CS, 0);
  esp_rom_delay_us(1);

  assert(ret==ESP_OK);
  }


void displayChar(uint8_t index, Cursor_t *cur) {
    for (int m =0; m < PSF_GLYPH_SIZE; m++) {
       sharpmem_buffer[(( (cur->y) * PSF_GLYPH_SIZE +m)*PXWIDTH + 8 * (cur->x)) / 8] = zap_vga16_psf[ index * PSF_GLYPH_SIZE +m];
    }
    updateRow(cur->y);
}


void clearDisplayBuffer() {
  memset(sharpmem_buffer, 0xFF, (PXWIDTH * PXHEIGHT) / 8);
}



