/* SPI Master example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "zap-vga16-raw-neg.h"
#include "keyboard_input.h"
#define PSF_GLYPH_SIZE 16

/*
 This example demonstrates the use of both spi_device_transmit as well as
 spi_device_queue_trans/spi_device_get_trans_result and pre-transmit callbacks.

 Some info about the ILI9341/ST7789V: It has an C/D line, which is connected to a GPIO here. It expects this
 line to be low for a command and high for data. We use a pre-transmit callback here to control that
 line: every transaction has as the user-definable argument the needed state of the D/C line and just
 before the transaction is sent, the callback will set this line to the correct state.
*/

#define SHARPMEM_BIT_WRITECMD (0x01) // 0x80 in LSB format otherwise 0x01
#define SHARPMEM_BIT_VCOM (0x02)     // Sent now using SPI_DEVICE_TXBIT_LSBFIRST
#define SHARPMEM_BIT_CLEAR (0x04)

#define ESP_HOST    SPI2_HOST // SPI2
#define PIN_NUM_MOSI 35
#define PIN_NUM_CLK  36
#define PIN_NUM_CS   38
#define PIN_NUM_VCOM   33
#define PIN_BLUE_LED   13

#define PXWIDTH 320
#define PXHEIGHT 240


#define KEY(r, c) ((r << 3) + c)
#define CUR( x, y ) (x + y*PXWIDTH/8)  

#define KBD_EVENT_QUEUE_LENGTH 32
#define KBD_EVENT_SIZE sizeof( uint8_t )

#define SPI_TAG "spi_protocol"
void displayInit(void);
void display_write_data(uint8_t addr, uint8_t data);


//// hold the queue structure.
///configSUPPORT_STATIC_ALLOCATION: Must be enabled in menuconfig?
StaticQueue_t kbd_StaticQueue;
uint8_t kbd_QueueStorage[ KBD_EVENT_QUEUE_LENGTH * KBD_EVENT_SIZE ];
QueueHandle_t keyboard;


spi_device_handle_t spi;
DMA_ATTR uint8_t *sharpmem_buffer = NULL;

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


void vcom_toggle_task(void *pvParameters) 
{
    gpio_set_direction(PIN_NUM_VCOM, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_BLUE_LED, GPIO_MODE_OUTPUT);
    while (1) {
        // Toggle the GPIO state
        gpio_set_level((gpio_num_t)PIN_NUM_VCOM, 0);
        gpio_set_level((gpio_num_t)PIN_BLUE_LED, 0);
        vTaskDelay(500 / portTICK_PERIOD_MS); // Delay for 1 second
        gpio_set_level((gpio_num_t)PIN_NUM_VCOM, 1);
        gpio_set_level((gpio_num_t)PIN_BLUE_LED, 1);
        vTaskDelay(500 / portTICK_PERIOD_MS); // Delay for 1 second
    }
}


void displayInit(void)
{
    // Start the VCOM toggling task
    xTaskCreate(&vcom_toggle_task, "vcom", 2048, NULL, 5, NULL);

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

/* This could be a prototype for the keyboard scan task, showing the queue feature to push key events */
//  static void vKeyboardScanTask( void *pvParameters )
//  {
//      uint8_t keyevent = ((0x61 << 1) + 1);    // Key 0x61 has ben pressed
//      BaseType_t xReturn; // Used to receive return value
//  
//      if (keyChanged) {
//          xReturn = xQueueSend( keyboard , (void *)&keyevent , 10);
//          // Determine whether the data is sent successfully through the return value
//          if (xReturn == pdTRUE) {
//              printf("Item Send: %d \n", keyevent);
//          }
//          else {
//              printf("Item Send FALSE\n");
//          }
//          vTaskDelay(1);
//      }
//  }
 
/* This could be a prototype for the keyboard scan task, showing the queue feature to push key events */
static void vKeyboardSimuTask( void *pvParameters )
{

    uint8_t keyevent[11] = {35+128,  18+128, 38+128, 38+128, 24+128, 53+128, 17+128, 24+128, 19+128, 38+128, 32+128 }; // all keydown events 
    for (int i=0; i<10; i++) {
    for (int m=0; m < 11; m++ ){
        //keyevent++;
        //if (xQueueSend( keyboard , (void *)&keyevent , portMAX_DELAY) == pdTRUE) {
        if (xQueueSend( keyboard , &keyevent[ m ] , portMAX_DELAY) == pdTRUE) {
            printf("Item Send: %d \n", keyevent[ m ]);
        }
        else {
            printf("Item Send FALSE\n");
        }
    }}
    vTaskDelete( NULL ); // needs to be called for the task to finish without errors.
}
 


/* And this could be the processing of the keyboard keys using the keymapping*/
static void vProcessKeyTask( void *pvParameters )
{
    uint8_t key = 0;   // received key-event data
    uint8_t fontchar = 0;
    Cursor_t *cur = (Cursor_t *) pvParameters;
    while (1) {
        if (xQueueReceive( keyboard , (void *)&key, portMAX_DELAY) == pdTRUE) {  //this is blocking inf.
	    bool keydown = (key & KEYDOWN_MASK);
	    bool modifier = (key & MOD_MASK);

	    if ( modifier ) {
		    printf("Key is a modifier \n");
		    if ( keydown ) KBD_MODS |= (key & KEY_MASK );
		    else KBD_MODS &= ~(key & KEY_MASK );
	    }
	    else if ( keydown ) {
		    Virtual_Key vk = keymap[ (key & KEY_MASK) ];
		    if (vk < VKCHAROFFSET) {
		        printf("Key is a control key (non printable) \n");
		    }
		    else {
		        fontchar = fontmap[vk - VKCHAROFFSET];
                        displayChar(fontchar, cur);
			cur->x++;
			if (cur->x == 40) {
			    cur->y++;
			    cur->x = 0;
			    if (cur->y == 14) cur->y = 0;
			}
	                //curx++;
			//if (curx > 39) { 
			//	curx = 0 ; 
			//	cury++;
			//	cury = cury %15;
			//}
		    }
	    }
	    /* Associate the key event with a key through the keymap,
	     * Then, if it is a modifier, change the modifiers byte, 
	     * Then, if check what does result from the combination of all modifiers,
	     * If it results in a system/control, call the respective function.
	     * If it results in a printable character,
	     * check if the combination produces another control secquence (either
	     * in the editor or in any other framework...(editor normal mode, wifi menu...)
	     * Depending on the status, the printable character is then
	     * interpreted as unicode to save the text, and simultaneously
	     * mapped to the font to produce the glyph on display.
	     */
        }
        else {
            printf("Item Receive FALSE\n");
        }
        vTaskDelay(1);
    }
}


void app_main(void)
{
    // Initialize Display
    displayInit();
    clearDisplay();

    static Cursor_t cursor; // initializes to position (0,0)
    cursor.mode = NORMAL;
    Cursor_t *cur = &cursor;
    //cur->x = 0;
    
    // Start reading the keyboard
    keyboard = xQueueCreateStatic( KBD_EVENT_QUEUE_LENGTH, // The number of items the queue can hold.
                         KBD_EVENT_SIZE,      // The size of each item in the queue
                         &( kbd_QueueStorage[ 0 ] ), // The buffer that will hold the items in the queue.
                         &kbd_StaticQueue ); // The buffer that will hold the queue structure.
    xTaskCreate(vProcessKeyTask, "keyboard", 2048, (void *) cur, 5, NULL);
    xTaskCreate(vKeyboardSimuTask, "keysimu", 2048, NULL, 5, NULL);
    while(1) {
     vTaskDelay(pdMS_TO_TICKS(10000)); 
    }
}

