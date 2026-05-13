| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# Typewrt V2 ESP related code
Implementation of the typewrt text editor in Espressiff IDF. The hardware interfaced is 
a Sharp 4.4" display, a custom 8x8 matrix keyboard (octal latch SN74HC573A), a RTC+SD card
Adafruit adalogger board (for time and data retention), a power button wired to the 
board reset pin, an external LED for notifications, and an on-demand BLE file sender.

## BLE file transfer

The Typewrt backend exposes an on-demand BLE GATT service named `Typewrt` for sending
text files to a phone. Bluetooth stays off until a transfer command is used.

| Command | Action |
| --- | --- |
| `:ble` | Queue and advertise the current editor buffer |
| `:ble path` | Queue and advertise a file from the SD card |
| `:ble status` | Show the current BLE state |
| `:ble off` | Cancel the pending transfer and turn BLE back off |

Connect from a BLE client such as nRF Connect or LightBlue, open service `0xffe0`,
and subscribe to characteristic `0xffe1`. The transfer is sent as notifications with
a `TYPEWRT-FILE` header, the raw file bytes, and a `TYPEWRT-END` footer. While a BLE
transfer is pending or active, the firmware keeps light sleep locked; after a transfer
finishes, BLE disconnects and light sleep is allowed again.

### Android companion app

A tiny Android receiver lives in `companion/typewrt-android`. Open that folder in
Android Studio, install the app on a phone, run `:ble` or `:ble path` on Typewrt,
then tap **Connect Typewrt** in the app. Received files are saved under
`Downloads/Typewrt`.

The app uses the same service/characteristic pair as the raw BLE workflow:
service `0xffe0`, TX notifications on `0xffe1`, and the `TYPEWRT-FILE` stream
format described above.

The companion can also export the latest received file through a reachable
`pandoc-server`, then upload the latest received or exported file to GitHub using
the repository contents REST API. `https://pandoc.org/app/` itself is browser-side
Pandoc WASM, so the native app expects a real `pandoc-server` URL such as
`http://192.168.1.20:3030/`.

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
 
