| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# Typewrt V2 ESP related code
Implementation of the typewrt text editor in Espressiff IDF. The hardware interfaced is 
a Sharp 4.4" display, a custom 8x8 matrix keyboard (octal latch SN74HC573A), a RTC+SD card
Adafruit adalogger board (for time and data retention), a power button wired to the 
board reset pin, an external LED for notifications, and an on-demand BLE file sender.

## Keyboard layouts

Nextvi starts in the English keymap. On the embedded Typewrt keyboard, hold `Alt`
and press one of the layout letters to switch:

| Shortcut | Layout |
| --- | --- |
| `Alt-e` | English |
| `Alt-s` | Spanish |
| `Alt-i` | Italian |
| `Alt-n` | Norwegian |
| `Alt-g` | German |
| `Alt-f` | French |
| `Alt-t` | Turkish |

The old `z1`, `z2`, `ze`, and `zf` keymap commands are no longer used. Layouts
that place accents on dead keys compose in insert and prompt input before the text
is written to the file.

## BLE file transfer

The Typewrt backend exposes an on-demand BLE GATT service named `Typewrt` for sending
text files to a phone and receiving files from a phone while the menu is open.
Bluetooth stays off until a transfer command is used.

| Command | Action |
| --- | --- |
| `:ble` | Queue and advertise the current editor buffer |
| `:ble path` | Queue and advertise a file from the SD card |
| `:ble status` | Show the current BLE state |
| `:ble off` | Cancel the pending transfer and turn BLE back off |
| Menu `s` | Toggle the selected file between unmarked and pending sync |
| Menu `b` or `ble send` | Send all pending `*` files |
| Menu `ble recv` | Receive file updates and remote-delete markers |

Connect from a BLE client such as nRF Connect or LightBlue, open service `0xffe0`,
and subscribe to characteristic `0xffe1`. The transfer is sent as notifications with
a `TYPEWRT-FILE` header, the raw file bytes, and a `TYPEWRT-END` footer. While a BLE
transfer is pending or active, the firmware keeps light sleep locked; after a transfer
finishes, BLE disconnects and light sleep is allowed again.

For receive mode, open the menu, run `ble recv`, connect from the phone, and write
one or more `TYPEWRT-FILE <bytes> <name>\n` streams to characteristic `0xffe2`.
The firmware writes each file directly into the menu's current directory; an optional
`TYPEWRT-END <bytes> <name>\n` line after the raw bytes is accepted and ignored.
The phone may also send `TYPEWRT-DELETE <path>\n`; Typewrt keeps the local copy and
marks it as deleted remotely.

### Android companion app

A tiny Android companion lives in `companion/typewrt-android`. Open that folder in
Android Studio and install the app on a phone. To receive marked files from Typewrt,
mark files in the menu with `s`, press `b`, then tap **From Typewrt** in the app.
Received files are saved into the app-owned `remote/` mirror.

To send phone-side updates into the embedded editor's SD card, open the Typewrt menu
in the target directory, run `ble recv`, then tap **To Typewrt** after a GitHub pull,
restore, or file-viewer delete request has queued updates. The Transfer tab includes a
small repository browser; queued updates are marked with `*`, and queued deletes with
`x`. The app uses service `0xffe0`, TX notifications on `0xffe1` for phone receive, RX
writes on `0xffe2` for Typewrt receive, and the `TYPEWRT-FILE` / `TYPEWRT-DELETE` stream
format described above.

The companion can also export the latest received file through a reachable
`pandoc-server` into an `output/` folder, pull a GitHub repository or subfolder into the
`remote/` mirror, commit all `remote/` changes back to GitHub, and restore `remote/` from
a recent commit. `https://pandoc.org/app/` itself is browser-side Pandoc WASM, so the
native app expects a real `pandoc-server` URL such as
`http://192.168.1.20:3030/`.

## Menu file browser

On embedded Typewrt, quitting the editor with commands such as `:q` enters `menu`
instead of ending the Nextvi task. The top status row shows the RTC date on the left,
the time centered, and battery state on the right with `C` for charging or `D` for
discharging. The bottom status row shows the current filesystem path inverted, prompts
remain plain, and the middle rows show one file or directory per line. The top row is
separated from the file list by a lowered 1 px rule and one spacer row. The SD card
mount point `/sdcard` is shown as the menu root.

Entry prefixes:

| Prefix | Meaning |
| --- | --- |
| `[+]` | Directory |
| `s` | File synced and unchanged since the last successful BLE send/receive |
| `*` | File marked pending for the next menu BLE send |
| `-` | File not marked for sync |
| `x` | File was deleted remotely; the local copy is preserved |

Normal menu keys:

| Key | Action |
| --- | --- |
| `j` / `k` | Move down/up |
| `h` | Go up one directory, like `cd ..` |
| `l` | Open the selected entry, entering directories |
| `Ctrl-D` / `Ctrl-U` | Page down/up |
| `g` / `G` | First/last entry |
| `Enter` or `o` | Open file, or enter directory |
| `b` | Send all pending `*` files by BLE |
| `d` | Delete selected file/directory after confirmation |
| `r` | Rename selected entry |
| `c` | Copy selected file |
| `R` | Refresh listing |
| `P` | Power off |
| `/` | Search the current listing by name |
| `:` | Open menu command prompt |
| `q` | Return to the current editor buffer |

Each listing row reserves right-hand columns for word count and last modification time.
Word counts use compact units such as `846` or `1.5 k`. Modification time is shown
as `HH:mm` for files changed today, and `Mon dd` for older files.

Menu commands include `cd PATH`, `cd ..`, `cd -`, `ls`, `ls -s`, `ls -rt`,
`ls *pattern*`, `mkdir PATH`, `open PATH`, `ble [send|recv|selected|PATH|status|off]`, `rtc [datetime]`,
`battery`, `off`, `rename`, `copy`, and `delete`. The command prompt temporarily
replaces the bottom status row. Directory listing state is remembered per directory,
including cursor position, scroll position, sort mode, and filter.

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
 
