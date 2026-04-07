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
#include "esp_timer.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
//#include "driver/uart.h"
#include "driver/spi_master.h"
//#include "driver/gpio_filter.h"
#include "soc/gpio_struct.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "esp_private/gpio.h"
//#include "esp_task_wdt.h"
//#include "esp_intr_alloc.h"

#include "zap-vga16-raw-neg.h"
#include "keyboard_input.h"


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






static const char *TAG = "mkbd";

#define IOSIZE 8

#define PIN_KBD_IO7  1
#define PIN_KBD_IO6  7
#define PIN_KBD_IO5  5
#define PIN_KBD_IO4  6
#define PIN_KBD_IO3 12
#define PIN_KBD_IO2 14
#define PIN_KBD_IO1 18
#define PIN_KBD_IO0 17
#define PIN_KBD_OE 11
#define PIN_KBD_LE 10

#define SCANTIMEOUT 500    // in number of scans
#define SCANPERIOD 2000  // us  Minimum response time (min debounce/denoise) is 8 consecutive periods.

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
              if ((KBD_BUFFER[(IOSIZE*row + col)]) == 0x00 ) {
                KBD_COLS[row] &= ~( 1  << col);
          	KBD_SCANCOUNT = SCANTIMEOUT;
                //ESP_LOGI(TAG, "Key  PRESSED  %d", (IOSIZE*row + col));
                //const char* kkk = "key event from ESP32s3\n";
                //uart_write_bytes(2, (const char *)kkk , strlen(kkk));
		key_event = KBDMAP[(IOSIZE*row + col)] | KEYDOWN_MASK;
                xQueueSend( keyboard , &key_event , portMAX_DELAY);
          	KBD_COLFLAGS[row] &= (~(1 << col) & ((1<< IOSIZE) -1));
              }
              if ((KBD_BUFFER[(IOSIZE*row + col)]) == 0xFF ) {
                KBD_COLS[row] |= ( 1  << col);
          	KBD_SCANCOUNT = SCANTIMEOUT;
                //ESP_LOGI(TAG, "Key RELEASED  %d", (IOSIZE*row + col));
                //const char* kkk = "key event from ESP32s3\n";
                //uart_write_bytes(2, (const char *)kkk , strlen(kkk));
		key_event = KBDMAP[(IOSIZE*row + col)];
                xQueueSend( keyboard , &key_event , portMAX_DELAY);
          	KBD_COLFLAGS[row] &= (~(1 << col) & ((1<< IOSIZE) -1));
              }
              scan &= (scan - 1);  // clears the lowest set bit
          }
          if ((~KBD_COLS[row]) & ((1UL << IOSIZE) -1)) KBD_NOKEY = false;
      }
      if (KBD_NOKEY) KBD_SCANCOUNT--;
    }
    else {  // GO TO STANDBY MODE
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
    for (int i=0; i < IOSIZE; i++) {
	gpio_wakeup_enable(KBD_IO[i], GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup(); 

    KBD_SCANCOUNT = SCANTIMEOUT;
    ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));

}


void app_main(void)
{


    // Setup the light sleep mode
    //esp_sleep_enable_gpio_wakeup(); 
    //esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
    //esp_wifi_stop();
    //esp_bt_controller_disable();

    esp_rom_delay_us(500);
    //xTaskCreate(vTaskStandBy, "standby", 2048, NULL, 5, NULL);
    // Keyboard start to work

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

    kbd_start();
    ESP_LOGI(TAG, "Keyboard started");
}
