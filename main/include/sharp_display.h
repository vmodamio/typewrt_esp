#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"



/* DISPLAY DEFINITIONS */
#define PSF_GLYPH_SIZE 16

#define SHARPMEM_BIT_WRITECMD (0x01) // 0x80 in LSB format otherwise 0x01
#define SHARPMEM_BIT_VCOM (0x02)     // Sent now using SPI_DEVICE_TXBIT_LSBFIRST
#define SHARPMEM_BIT_CLEAR (0x04)

#define ESP_HOST    SPI2_HOST // SPI2
#define PIN_NUM_MOSI 35
#define PIN_NUM_CLK  36
#define PIN_NUM_CS   38
//#define PIN_NUM_VCOM   33
#define PIN_BLUE_LED   13

#define PXWIDTH 320
#define PXHEIGHT 240


#define KEY(r, c) ((r << 3) + c)
#define CUR( x, y ) (x + y*PXWIDTH/8)  

#define KBD_EVENT_QUEUE_LENGTH 32
#define KBD_EVENT_SIZE sizeof( uint8_t )

#define SPI_TAG "spi_protocol"

//// hold the queue structure.
///configSUPPORT_STATIC_ALLOCATION: Must be enabled in menuconfig?
StaticQueue_t kbd_StaticQueue;
extern uint8_t kbd_QueueStorage[ KBD_EVENT_QUEUE_LENGTH * KBD_EVENT_SIZE ];
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

void displayInit(void);

void display_write_data(uint8_t addr, uint8_t data);

void setPixel(int16_t x, int16_t y, uint16_t color);

uint8_t getPixel(uint16_t x, uint16_t y); 

void clearDisplay(void); 

void refreshDisplay(void); 

void updateRow(uint8_t row); 

void displayChar(uint8_t index, Cursor_t *cur); 

void clearDisplayBuffer(); 



