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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/uart.h"
//#include "driver/gpio_filter.h"
#include "soc/gpio_struct.h"
#include "soc/soc.h"
#include "soc/gpio_reg.h"
#include "esp_private/gpio.h"
//#include "esp_task_wdt.h"
//#include "esp_intr_alloc.h"

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
#define SCANPERIOD 1200  // us
#define KBD_INTR_FLAG (ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_EDGE | ESP_INTR_FLAG_LOWMED)
//#define KBD_INTR_FLAG (ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LOWMED)

//#define GPIO_PIN_INT_ENA_M  (BIT(13))
//static IRAM_ATTR void mask_gpio_interrupt_direct(int gpio_num) {
//    // GPIO_PIN0_REG + (gpio_num * 4) is the register address
//    CLEAR_PERI_REG_MASK(GPIO_PIN0_REG + (gpio_num * 4), GPIO_PIN_INT_ENA_M);
//}
//
//static IRAM_ATTR void unmask_gpio_interrupt_direct(int gpio_num) {
//    // GPIO_PIN0_REG + (gpio_num * 4) is the register address
//    SET_PERI_REG_MASK(GPIO_PIN0_REG + (gpio_num * 4), GPIO_PIN_INT_ENA_M);
//}
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
static portMUX_TYPE KBD_MUTEX = portMUX_INITIALIZER_UNLOCKED;
volatile int KBD_SCANCOUNT;
volatile bool KBD_BUSY = true;    // ignores interrupt
volatile bool KBD_NOKEY = true;   // wether no keys are pressed
volatile bool KBD_SCAN = false;   // do a full matrix scan iteration (set periodically by the timer)
volatile bool KBD_STDBY = false;

static inline IRAM_ATTR void cycle(uint32_t cycles) {
     // One cycle at 240 MHz is 4.16ns, 160 MHz is 6.25 ns
     for (int i=0; i < cycles; i++) __asm__ __volatile__("nop");
}

static inline IRAM_ATTR void kbd_isr(void *arg)
{
    uint32_t status = GPIO.status;  // identify which pin triggered the interrupt
    GPIO.status_w1tc = status;  // clear the status bit
    int pin = __builtin_ffs(status) -1;
	 
    if (KBD_BUSY) {
        //ESP_EARLY_LOGI(TAG, "ISR busy [][][]");
        return;
    }
    else {
	taskENTER_CRITICAL_ISR(&KBD_MUTEX);
	KBD_BUSY = true;
        ESP_EARLY_LOGI(TAG, "Entered in the ISR...GPIO %d", pin);
        KBD_SCANCOUNT = SCANTIMEOUT;
        ESP_ERROR_CHECK(esp_timer_start_periodic(KBD_SCAN_TIMER, SCANPERIOD));
        //ESP_ERROR_CHECK(esp_timer_start_once(mkbd->scan_timer, 20000));
	taskEXIT_CRITICAL_ISR(&KBD_MUTEX);
    }
}

static IRAM_ATTR void kbd_scan(void* arg)
//{
//	taskENTER_CRITICAL_ISR(&KBD_MUTEX);
//	KBD_SCAN = true;
//	taskEXIT_CRITICAL_ISR(&KBD_MUTEX);
//}
//
//
//static IRAM_ATTR void kbd_run(void* arg)
{
    ///kbd_t *kbd = (kbd_t *)arg;
    int col = -1;
    int row = -1;

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
                ESP_LOGI(TAG, "Key  PRESSED  %d", (IOSIZE*row + col));
                const char* kkk = "key event from ESP32s3\n";
                uart_write_bytes(2, (const char *)kkk , strlen(kkk));
          	KBD_COLFLAGS[row] &= (~(1 << col) & ((1<< IOSIZE) -1));
              }
              if ((KBD_BUFFER[(IOSIZE*row + col)]) == 0xFF ) {
                KBD_COLS[row] |= ( 1  << col);
          	KBD_SCANCOUNT = SCANTIMEOUT;
                ESP_LOGI(TAG, "Key RELEASED  %d", (IOSIZE*row + col));
                const char* kkk = "key event from ESP32s3\n";
                uart_write_bytes(2, (const char *)kkk , strlen(kkk));
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
      ESP_LOGI(TAG, "timer stopped, trying to enter light sleep...");
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
      //ESP_LOGI(TAG, "----------STANDBY Mode   ");
      //taskENTER_CRITICAL(&KBD_MUTEX);
      ////KBD_STDBY = true;
      //KBD_BUSY = false;
      //taskEXIT_CRITICAL(&KBD_MUTEX);
      // Then goto sleep.
      for (int m=0; m < IOSIZE; m++) {
        GPIO.pin[KBD_IO[m]].int_ena = 0;
        GPIO.pin[KBD_IO[m]].int_type = 0;
        GPIO.pin[KBD_IO[m]].int_type = 4;
        gpio_wakeup_enable(KBD_IO[m], GPIO_INTR_LOW_LEVEL);
      }
      GPIO.status_w1tc = KBD_IO_MASK;
      esp_sleep_enable_gpio_wakeup(); 
      //KBD_BUSY = false;
      //for (int m=0; m < IOSIZE; m++) GPIO.pin[KBD_IO[m]].int_ena = 1;
      // Feed the task watchdog
      //esp_task_wdt_reset();
      ESP_ERROR_CHECK(esp_light_sleep_start());

      //esp_sleep_disable_gpio_wakeup(); 
      for (int m=0; m < IOSIZE; m++) {
        gpio_wakeup_disable(KBD_IO[m]);
        GPIO.pin[KBD_IO[m]].int_type = 0;
        GPIO.pin[KBD_IO[m]].int_type = 2;
        GPIO.pin[KBD_IO[m]].int_ena = 1;
      }
      GPIO.status_w1tc = KBD_IO_MASK;

      taskENTER_CRITICAL(&KBD_MUTEX);
      //KBD_STDBY = true;
      KBD_BUSY = false;
      taskEXIT_CRITICAL(&KBD_MUTEX);
      ESP_LOGI(TAG, "woken up...");
      //esp_light_sleep_start();
      /* THe light sleep is triggering the watchdog.... instead, start incorporating the 
       * key event queue, as for the sharp display, and arrage the light_sleep in that task,
       * when there is no queue and all the keys are processed.  */
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
        .intr_type = GPIO_INTR_NEGEDGE,  //  GPIO_INTR_LOW_LEVEL   
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

    uart_config_t uart_conf = {
	    .baud_rate = 115200,
	    .data_bits = UART_DATA_8_BITS,
	    .parity    = UART_PARITY_DISABLE,
	    .stop_bits  = UART_STOP_BITS_1,
            .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
	    .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(2, 512 , 0, 0, NULL, ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(uart_param_config(2, &uart_conf));
    ESP_ERROR_CHECK(uart_set_pin(2, 8, 9, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    //uart_write_bytes(2, (const char *) data, len);
    const char* data = "Trying UART from ESP32s3\n";
    uart_write_bytes(2, (const char *)data , strlen(data));


    GPIO.out_w1ts = (1UL << KBD_OE); // set OE high (active low)
    ESP_LOGI(TAG, "*** Set OE (pin %d) HIGH: %d", KBD_OE, GPIO.out);
    cycle(160);
    GPIO.enable_w1ts = KBD_IO_MASK;  // set gpios to output
    cycle(320);
    ESP_LOGI(TAG, "*** Set IO pins (%d) to OUTPUT: %d",KBD_IO_MASK, GPIO.enable);
    GPIO.out_w1tc = KBD_IO_MASK;  // set all pins to low (for ROWS)
    ESP_LOGI(TAG, "*** Set ALL IO pins (%d) to LOW: %d", KBD_IO_MASK, GPIO.out);
    GPIO.out_w1ts = (1UL << KBD_LE); // set LE high to transfer to OUTPUT (D->Q)
    cycle(40);
    ESP_LOGI(TAG, "*** Set LE (pin %d) to HIGH: %d", KBD_LE, GPIO.out);
    GPIO.out_w1tc = (1UL << KBD_LE); // set LE low to latch
    cycle(320);
    ESP_LOGI(TAG, "*** Set LE (pin %d) to LOW: %d", KBD_LE, GPIO.out);
    cycle(40);
    GPIO.enable_w1tc = KBD_IO_MASK;  // set gpios to input (they are pulled up anyhow)
    cycle(320);
    ESP_LOGI(TAG, "*** Set IO pins (%d) to INPUT: %d",KBD_IO_MASK, GPIO.enable);
    GPIO.out_w1tc = (1UL << KBD_OE); // set OE low to enable output
    cycle(160);
    ESP_LOGI(TAG, "*** Set OE (pin %d)LOW: %d", KBD_OE,  GPIO.out);
    cycle(1600);
    uint32_t readout = GPIO.in;
    ESP_LOGI(TAG, "*** READ IO INPUTS: %d", readout);

    // Clear all interrupts pending before initializing the ISR
    gpio_intr_disable(GPIO_NUM_2);   // GPIO 02 is used in UM feather S3 as the VBAT SENSE IO
    uint32_t status = GPIO.status;  
    GPIO.status_w1tc = status;  

    gpio_isr_register(kbd_isr, (void*)NULL, KBD_INTR_FLAG, NULL);
    for (int i=0; i < IOSIZE; i++) {
        gpio_intr_enable(KBD_IO[i]);
	//gpio_wakeup_enable(KBD_IO[i], GPIO_INTR_LOW_LEVEL);
    }

}

static inline void vTaskStandBy(void * pvParameters) {
            ESP_LOGI(TAG, "----------STANDBY task ceated   ");
    //esp_sleep_enable_gpio_wakeup(); 
    //esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
    //esp_wifi_stop();
    //esp_bt_controller_disable();
    for ( ;; ) {
        if (KBD_STDBY) {
            ESP_LOGI(TAG, "----------STANDBY Mode   ");
            for (int m=0; m < IOSIZE; m++) {
              GPIO.pin[m].int_ena = 0;
              GPIO.pin[m].int_type = 0;
              //GPIO.pin[m].int_type = 4;
              gpio_wakeup_enable(KBD_IO[m], GPIO_INTR_LOW_LEVEL);
            }
            GPIO.status_w1tc = KBD_IO_MASK;
            esp_sleep_enable_gpio_wakeup(); 
            for (int m=0; m < IOSIZE; m++) GPIO.pin[m].int_ena = 1;
            ESP_ERROR_CHECK(esp_light_sleep_start());

            for (int m=0; m < IOSIZE; m++) {
              GPIO.pin[m].int_ena = 0;
              gpio_wakeup_disable(KBD_IO[m]);
              GPIO.pin[m].int_type = 0;
              GPIO.pin[m].int_type = 2;
              GPIO.pin[m].int_ena = 1;
            }
            GPIO.status_w1tc = KBD_IO_MASK;
            taskENTER_CRITICAL(&KBD_MUTEX);
            KBD_BUSY = false;
            KBD_STDBY = false;
            taskEXIT_CRITICAL(&KBD_MUTEX);
        }
	else vTaskDelay(pdMS_TO_TICKS(1));
    }
}


void app_main(void)
{

    kbd_start();

    // Setup the light sleep mode
    //esp_sleep_enable_gpio_wakeup(); 
    //esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_ON);
    //esp_wifi_stop();
    //esp_bt_controller_disable();

    esp_rom_delay_us(500);
    //xTaskCreate(vTaskStandBy, "standby", 2048, NULL, 5, NULL);
    // Keyboard start to work
    ESP_LOGI(TAG, "Keyboard started");
    KBD_BUSY = false;
}
