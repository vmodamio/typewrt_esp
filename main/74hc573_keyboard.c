
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
                xQueueSend( keyboard , &key_event , portMAX_DELAY);
          	KBD_COLFLAGS[row] &= (~(1 << col) & ((1<< IOSIZE) -1));
              }
              if (((KBD_BUFFER[(IOSIZE*row + col)]) == 0xFF ) && ~(KBD_COLS[row] & (1 << col)) ) {
                KBD_COLS[row] |= ( 1  << col);
                //KBD_BUFFER[(IOSIZE*row + col)] = 0x00; 
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


