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
