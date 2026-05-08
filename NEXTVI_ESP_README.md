# nextvi ESP Editor

This firmware embeds a `nextvi`-style editor as the main ESP32-S3 application. It is based on the local terminal editor in:

`/home/vik/typewriter/typewriter_V2/editors/nextvi`

The ESP version does not use a terminal. It reads key events from `typewrt_keyboard` and redraws text rows through `typewrt_display`.

## Current Modes

| Mode | Status | Notes |
| --- | --- | --- |
| Normal | Implemented | Starts here, matching vi/nextvi behavior. |
| Insert | Implemented | `i`, `I`, `a`, `A`, `o`, `O`, `s`, `c` enter insert edits. |
| Replace | Implemented | `R` replaces existing characters until Escape. |
| Visual | Implemented | `v` character selection, `V` line selection. |
| Command | Partial | `:` prompt exists, but full ex is not ported. |
| Search | Partial | `/`, `?`, `n`, `N` use plain substring search, not regex. |

## Status Row

The status row is hidden by default so the editor can use all 15 physical text rows on the display. It appears temporarily for command feedback and errors, and it stays visible while entering `:` commands or `/` and `?` searches. After a short timeout it is replaced by the normal text view again.

## Startup Splash

When no text file is loaded, the editor starts with a 320x160 monochrome bitmap splash in the top two thirds of the display. The splash contains `Typewrt` rendered from `MomoTrustDisplay-Regular.ttf`, with `Mark 2 (v2.0)` below it in the editor VGA font, right-aligned to the wordmark. The editable cursor line is shown below the splash, just above the status row.

Entering insert mode keeps the splash visible. The first printable key, Enter, or Tab clears the splash and starts editing at the first buffer line.

## Implemented Normal Commands

| Command | ESP behavior |
| --- | --- |
| `h j k l`, arrows | Move cursor. |
| `0`, `^`, `$` | Move to start, first nonblank, end of line. |
| `w W e E b B` | Word motions. |
| `gg`, `G`, `H`, `M`, `L` | Line and screen-position jumps. |
| `%` | Match `()`, `[]`, `{}`. |
| `f F t T`, `;`, `,` | Find character on current line. |
| Counts | Supported for common motions and edits, for example `3j`, `5x`, `2dd`. |
| `x`, `X`, `s`, `S` | Delete/change character or line. |
| `d`, `c`, `y` + motion | Delete, change, yank regions. |
| `dd`, `cc`, `yy` | Line delete/change/yank. |
| `D`, `C`, `Y` | To end of line / yank line. |
| `p`, `P` | Put yanked text. |
| `J` | Join lines. |
| `u`, Ctrl-R | Undo and redo. |
| `m`, backtick, `'` | Set and jump to marks `a` through `z`. |
| `<`, `>` | Shift selected/operator lines by two spaces. |
| `~`, `gu`, `gU` | Change case. |
| `v`, `V` | Visual and visual-line selection. |
| `z Enter`, `z.`, `z-` | Reposition viewport. |

## Hard Wrapping

The ESP editor now ports the terminal `nextvi` hard-wrap model:

- The hard-wrap width is fixed to the display width: `TYPEWRT_DISPLAY_TEXT_COLUMNS`, currently 40 columns.
- Insert edits hard-wrap live after typed characters, Enter, Backspace, Delete, and Tab.
- Pasting and joining lines reflow the affected paragraph.
- `gw` reflows the current wrapped paragraph.
- `gq` reflows the whole buffer.
- `:gw` and `:gq` are also available.
- Generated continuation lines are prefixed internally with the same hidden U+200B marker used by terminal `nextvi`.
- The display renderer hides the continuation marker, so wrapped lines appear as normal text rows.

This is intentionally hard wrapping, not soft wrapping: the buffer is rewritten into physical lines.

## Command Prompt

| Command | ESP behavior |
| --- | --- |
| `:w`, `:write` | Stub: save waits for SD card. |
| `:e`, `:edit` | Stub: load waits for SD card. |
| `:wq`, `:x` | Calls save stub. |
| `:%d` | Clears the buffer. |
| `:gw` | Reflows current paragraph. |
| `:gq` | Reflows whole buffer. |
| Other ex commands | Not implemented yet. |

## Differences From Terminal nextvi

| Area | Terminal nextvi | ESP editor |
| --- | --- | --- |
| Terminal I/O | Full terminal renderer and input queue. | Direct display row refresh and keyboard queue. |
| Files | Reads/writes real files. | Stubbed until SD card support is added. |
| Ex mode | Large command set in `ex.c`. | Small command prompt only. |
| Regex | Original regex engine. | Plain substring search for now. |
| Multiple buffers | Supported. | Not implemented. |
| Registers/macros | Richer original support. | One unnamed yank buffer, no macro execution. |
| Unicode rendering | Original `ren.c`/`uc.c` width handling. | ASCII-oriented editor buffer; keyboard maps printable ASCII for now. |
| Hard wrap | `conf_hwwidth`, U+200B continuation markers. | Same marker model, fixed to display width. |
| Status | Terminal status/message area. | One display status row. |

## Implementation Files

- `main/nextvi_esp.c`: editor state, vi commands, hard wrapping, keyboard event handling.
- `include/nextvi_esp.h`: app entry point.
- `include/typewrt_display.h`, `main/typewrt_display.c`: display API used by the editor.
- `typewrt_display_draw_bitmap(...)`: renders 1-bit row-major bitmap images, with set bits drawn as black pixels.
- `include/typewrt_keyboard.h`, `main/typewrt_keyboard_api.c`: keyboard API used by the editor.
