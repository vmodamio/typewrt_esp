| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# Typewrt V2 ESP related code
Implementation of the typewrt text editor in Espressiff IDF. The hardware interfaced is 
a Sharp 4.4" display, a custom 8x8 matrix keyboard (octal latch SN74HC573A), a RTC+SD card
Adafruit adalogger board (for time and data retention), a power button wired to the 
board reset pin, an external LED for notifications, and an on-demand BLE file sender.

## Keyboard layouts

Nextvi starts in the English keymap, which is US ISO: US punctuation and the ISO
extra key left of `Z` as `<` unshifted and `>` with Shift. On the embedded
Typewrt keyboard, hold `Alt` and press one of the layout letters to switch:

| Shortcut | Layout |
| --- | --- |
| `Alt-e` | English (US ISO) |
| `Alt-s` | Spanish |
| `Alt-i` | Italian |
| `Alt-n` | Norwegian |
| `Alt-g` | German |
| `Alt-f` | French |
| `Alt-t` | Turkish |
| `Alt-k` | Colemak |

The old `z1`, `z2`, `ze`, and `zf` keymap commands are no longer used. Layouts
that place accents on dead keys compose in insert and prompt input before the text
is written to the file.

Keymap changes are intentionally text-entry scoped: insert mode and prompts use
the selected layout, while vi normal-mode command letters stay on the physical
English layout for command muscle memory. Character arguments typed after normal
commands such as `f`, `F`, `t`, `T`, and `r` use the selected layout.

Because `Esc` occupies the usual key left of `1`, `Alt-Esc` enters that layout's
unshifted top-left symbol and `Shift-Alt-Esc` enters the shifted symbol. In
English these are `` ` `` and `~`.

After a layout switch, the status row briefly shows the active layout, for example
`Keyboard [es]`. Press `Alt-Space` to open the keyboard layout helper for the
current layout. The helper shows the normal and shifted key layers, including the
top row and left/right letter blocks, plus the current `Alt-Esc` symbols. For
English it shows the ISO `<` / `>` key; for layouts with dead keys it shows a
short reminder such as `´+e=é`. Press any key to close it and return to the
editor.

On the embedded keyboard, `Ctrl-Tab` is an alias for `Ctrl-N` in every editor
context, and `Ctrl-Shift-Tab` is an alias for `Ctrl-R`.

In normal mode, `Cmd` scrolls one full page down while keeping the cursor on
the same screen row when possible; `Ctrl-Cmd` scrolls one full page up the same
way.

In insert mode, pressing `Cmd` temporarily returns to normal-mode command parsing
for one instruction, then returns to insert mode. For example, `Cmd` followed by
`dw` deletes the next word and resumes inserting text.

## Editor global marks

Nextvi's `Ctrl-T` global mark slots persist on the SD card across reboot. Use
`Ctrl-T` to set slot `0`, or `1` through `9` before `Ctrl-T` to set that
numbered slot. Jump back to a slot with the normal mark motions, such as `'3`
or `` `3``. In Nextvi ex mode, `:gmarks` prints the saved slots. In the menu
command prompt, `:gmarks` opens a picker for the same slots.

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
a `TYPEWRT-FILE2` header carrying byte count and modification time, the raw file bytes,
and a `TYPEWRT-END` footer. Receivers still accept the older `TYPEWRT-FILE` header.
While a BLE
transfer is pending or active, the firmware keeps light sleep locked; after a transfer
finishes, BLE disconnects and light sleep is allowed again.

For receive mode, open the menu, run `ble recv`, connect from the phone, and write
one or more `TYPEWRT-FILE2 <bytes> <mtime> <name>\n` streams to characteristic `0xffe2`.
The firmware writes each file directly into the menu's current directory and preserves
the transferred FATFS modification time; an optional
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
writes on `0xffe2` for Typewrt receive, and the `TYPEWRT-FILE2` / `TYPEWRT-DELETE` stream
format described above.

The companion can also export selected `remote/` files through a reachable
`pandoc-server` into an `output/` folder, using `.yaml` or `.yml` selections as Pandoc
metadata and exposing completed exports to other Android apps, pull a GitHub repository
or subfolder into the `remote/` mirror, commit locally tracked `remote/` changes back to
GitHub, and restore `remote/` from a recent commit. A small `.typewrt-sync.json` manifest
tracks GitHub blob SHAs and local hashes so commits avoid re-comparing the whole
repository.
`https://pandoc.org/app/` itself is browser-side Pandoc WASM, so the native app expects a
real `pandoc-server` URL such as
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
| `0` / `$` | Pan a truncated selected name to start/end |
| `Ctrl-D` / `Ctrl-U` | Page down/up |
| `g` / `G` | First/last entry |
| `Enter` or `o` | Open file, or enter directory |
| `b` | Send all pending `*` files by BLE |
| `d` | Delete selected file/directory; non-empty directories require an extra confirmation |
| `r` | Rename selected entry |
| `c` | Copy selected file |
| `R` | Refresh listing |
| `P` | Power off |
| `/` | Search the current listing by name |
| `:` | Open menu command prompt |
| `Ctrl-N` / `Ctrl-Tab` | Return to Nextvi and run normal-mode `Ctrl-N` buffer navigation |
| `Ctrl-^` | Return to Nextvi and switch to the previous buffer |
| `Ctrl-_` | Open the in-menu buffer picker |
| `q` | Return to the current editor buffer |

The buffer picker shows the same main-buffer indices used by Nextvi's `:b` command.
Use `j`/`k`, `Ctrl-D`/`Ctrl-U`, `g`/`G`, or a single digit to choose a buffer;
`Enter`, `l`, or `o` switches via Nextvi's existing `:bN` command. `q` or `Esc`
returns to the file browser. From the menu command prompt, `:b` and `:buffer`
without an argument open the same picker, while explicit buffer commands such as
`:b2` or `:b 2` are passed through to Nextvi's ex parser.

The global mark picker opens from the menu command prompt with `:gmarks`. It shows
the persistent `Ctrl-T` slots as `slot line;offset path`; use `j`/`k`,
`Ctrl-D`/`Ctrl-U`, `g`/`G`, a digit, `Enter`, `l`, or `o` to choose a mark.

Each listing row reserves right-hand columns for word count and last modification time.
Word counts use compact units such as `846` or `1.5 k`. Modification time is shown
as `HH:mm` for files changed today, `Mon dd` for older files this year, and
`dd.MM.YY` for previous years.

Dotfiles are hidden in the browser by default; use `:ls -a` to show them.

Menu commands include `cd PATH`, `cd ..`, `cd -`, `ls`, `ls -a`, `ls -s`, `ls -rt`,
`ls *pattern*`, `mkdir PATH`, `open PATH`, `gmarks`,
`ble [send|recv|selected|PATH|status|off]`, `rtc [datetime]`, `battery`, `mem`,
`off`, `rename`, `copy`, `delete`, and forwarded Nextvi ex commands.
The command prompt temporarily
replaces the bottom status row. Directory listing state is remembered per directory,
including cursor position, scroll position, sort mode, and filter.

## LED notifications

The external LED is active-low on `PIN_LEDN`.

| State | Indication |
| --- | --- |
| USB powered, idle | Steady on |
| Battery powered, idle | Off |
| Boot | 2 fast pulses |
| SD card write | Fast blink until the write finishes |
| BLE send starts | 1 fast blink |
| BLE send finishes | 2 fast blinks |
| Battery below 25%, discharging | 3 short pulses, repeated every 5 minutes |
| Battery below 10% and at or above 7%, discharging | Rapid blink for 2 seconds, repeated every minute |
| Battery below 7%, not USB-powered | `Battery low` popup every 30 seconds; no battery LED warning |

Battery checks are periodic: every 10 minutes at 50% or above, every 5 minutes from
25% to 49.9%, and every minute below 25%. SD card write notifications take priority over
boot, BLE send, and battery warnings.
 
