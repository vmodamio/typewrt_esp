#include "esp_timer.h"

esp_timer_handle_t KBD_SCAN_TIMER;
static const char *TAG = "mkbd";

static inline IRAM_ATTR void cycle(uint32_t cycles) {
     // One cycle at 240 MHz is 4.16ns, 160 MHz is 6.25 ns
     for (int i=0; i < cycles; i++) __asm__ __volatile__("nop");
}


static IRAM_ATTR void kbd_scan(void* arg);

void kbd_start();



