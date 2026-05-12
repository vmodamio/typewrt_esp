| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# Typewrt V2 ESP related code
Implementation of the typewrt text editor in Espressiff IDF. The hardware interfaced is 
a Sharp 4.4" display, a custom 8x8 matrix keyboard (octal latch SN74HC573A), a RTC+SD card
Adafruit adalogger board (for time and data retention), a power button wired to the 
board reset pin and an external LED for notifications.

## LED notifications

The external LED is active-low on `PIN_LEDN`.

| State | LED indication |
| --- | --- |
| USB powered, idle | Steady on |
| Battery powered, idle | Off |
| Boot | 2 visible pulses, 250 ms each |
| SD card write | Fast blink until the write finishes |
| Battery below 25%, discharging | 3 short pulses, repeated every 5 minutes |
| Battery below 10%, discharging | Rapid blink for 2 seconds, repeated every 2 minutes |

Battery checks are periodic: every 5 minutes at 50% or above, every 2 minutes from 25%
to 49.9%, and every minute below 25%. SD card write notifications take priority over
boot and battery warnings.
 
