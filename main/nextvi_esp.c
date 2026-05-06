#include "nextvi_esp.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "keyboard_input.h"
#include "typewrt_display.h"
#include "typewrt_keyboard.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define NEXTVI_INITIAL_LINE_CAP 32
#define NEXTVI_INITIAL_LINE_LEN 64

typedef enum {
    NEXTVI_MODE_NORMAL,
    NEXTVI_MODE_INSERT,
} NextviMode;

typedef struct {
    char **lines;
    int *lengths;
    int line_count;
    int line_cap;
    int row;
    int col;
    int top;
    int left;
    int preferred_col;
    bool modified;
    NextviMode mode;
    char status[TYPEWRT_DISPLAY_TEXT_COLUMNS + 1];
} NextviEditor;

static void editor_set_status(NextviEditor *ed, const char *status);

static bool nextvi_storage_load(NextviEditor *ed, const char *path)
{
    editor_set_status(ed, "load waits for SD card");
    return false;
}

static bool nextvi_storage_save(NextviEditor *ed, const char *path)
{
    editor_set_status(ed, "save waits for SD card");
    return false;
}

static void die_oom(void)
{
    printf("nextvi: out of memory\n");
    abort();
}

static void *xmalloc(size_t size)
{
    void *p = malloc(size);
    if (!p) {
        die_oom();
    }
    return p;
}

static void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (!p) {
        die_oom();
    }
    return p;
}

static char *line_new_empty(void)
{
    char *line = xmalloc(NEXTVI_INITIAL_LINE_LEN);
    line[0] = '\0';
    return line;
}

static void editor_set_status(NextviEditor *ed, const char *status)
{
    snprintf(ed->status, sizeof(ed->status), "%s", status);
}

static void editor_set_keyboard_debug_status(NextviEditor *ed, const char *prefix, uint8_t event, int vk)
{
    typewrt_keyboard_debug_t debug;
    typewrt_keyboard_debug_snapshot(&debug);
    snprintf(ed->status, sizeof(ed->status), "%s e%02x vk%d s%lu p%lu r%lu",
             prefix,
             event,
             vk,
             (unsigned long)debug.scans,
             (unsigned long)debug.press_events,
             (unsigned long)debug.release_events);
}

static void editor_init(NextviEditor *ed)
{
    memset(ed, 0, sizeof(*ed));
    ed->line_cap = NEXTVI_INITIAL_LINE_CAP;
    ed->lines = xmalloc(sizeof(ed->lines[0]) * ed->line_cap);
    ed->lengths = xmalloc(sizeof(ed->lengths[0]) * ed->line_cap);
    ed->lines[0] = line_new_empty();
    ed->lengths[0] = 0;
    ed->line_count = 1;
    ed->mode = NEXTVI_MODE_NORMAL;
    editor_set_status(ed, "normal - press i to insert");
}

static void editor_free(NextviEditor *ed)
{
    for (int i = 0; i < ed->line_count; i++) {
        free(ed->lines[i]);
    }
    free(ed->lines);
    free(ed->lengths);
}

static void ensure_line_capacity(NextviEditor *ed, int needed)
{
    if (needed <= ed->line_cap) {
        return;
    }

    while (ed->line_cap < needed) {
        ed->line_cap *= 2;
    }
    ed->lines = xrealloc(ed->lines, sizeof(ed->lines[0]) * ed->line_cap);
    ed->lengths = xrealloc(ed->lengths, sizeof(ed->lengths[0]) * ed->line_cap);
}

static void ensure_text_capacity(NextviEditor *ed, int row, int needed)
{
    int cap = ed->lengths[row] + 1;
    while (cap < needed) {
        cap *= 2;
        if (cap < NEXTVI_INITIAL_LINE_LEN) {
            cap = NEXTVI_INITIAL_LINE_LEN;
        }
    }
    ed->lines[row] = xrealloc(ed->lines[row], cap);
}

static void insert_line(NextviEditor *ed, int row, char *line, int len)
{
    ensure_line_capacity(ed, ed->line_count + 1);
    memmove(&ed->lines[row + 1], &ed->lines[row], sizeof(ed->lines[0]) * (ed->line_count - row));
    memmove(&ed->lengths[row + 1], &ed->lengths[row], sizeof(ed->lengths[0]) * (ed->line_count - row));
    ed->lines[row] = line;
    ed->lengths[row] = len;
    ed->line_count++;
}

static void delete_line(NextviEditor *ed, int row)
{
    if (ed->line_count == 1) {
        ed->lines[0][0] = '\0';
        ed->lengths[0] = 0;
        ed->row = 0;
        ed->col = 0;
        return;
    }

    free(ed->lines[row]);
    memmove(&ed->lines[row], &ed->lines[row + 1], sizeof(ed->lines[0]) * (ed->line_count - row - 1));
    memmove(&ed->lengths[row], &ed->lengths[row + 1], sizeof(ed->lengths[0]) * (ed->line_count - row - 1));
    ed->line_count--;
    if (ed->row >= ed->line_count) {
        ed->row = ed->line_count - 1;
    }
    if (ed->col > ed->lengths[ed->row]) {
        ed->col = ed->lengths[ed->row];
    }
}

static void clamp_cursor(NextviEditor *ed)
{
    if (ed->row < 0) {
        ed->row = 0;
    }
    if (ed->row >= ed->line_count) {
        ed->row = ed->line_count - 1;
    }
    if (ed->col < 0) {
        ed->col = 0;
    }
    if (ed->col > ed->lengths[ed->row]) {
        ed->col = ed->lengths[ed->row];
    }
}

static void keep_cursor_visible(NextviEditor *ed)
{
    if (ed->row < ed->top) {
        ed->top = ed->row;
    } else if (ed->row >= ed->top + TYPEWRT_DISPLAY_TEXT_ROWS) {
        ed->top = ed->row - TYPEWRT_DISPLAY_TEXT_ROWS + 1;
    }

    if (ed->col < ed->left) {
        ed->left = ed->col;
    } else if (ed->col >= ed->left + TYPEWRT_DISPLAY_TEXT_COLUMNS) {
        ed->left = ed->col - TYPEWRT_DISPLAY_TEXT_COLUMNS + 1;
    }
}

static void editor_draw(NextviEditor *ed)
{
    clamp_cursor(ed);
    keep_cursor_visible(ed);

    for (int screen_row = 0; screen_row < TYPEWRT_DISPLAY_TEXT_ROWS; screen_row++) {
        int buffer_row = ed->top + screen_row;
        if (buffer_row < ed->line_count) {
            const char *line = ed->lines[buffer_row];
            int len = 0;
            if (ed->left < ed->lengths[buffer_row]) {
                line += ed->left;
                len = ed->lengths[buffer_row] - ed->left;
            } else {
                line = "";
                len = 0;
            }
            int cursor_col = buffer_row == ed->row ? ed->col - ed->left : -1;
            typewrt_display_draw_text_line(screen_row, line, len, cursor_col, buffer_row == ed->row);
        } else {
            typewrt_display_draw_text_line(screen_row, "~", 1, -1, 0);
        }
    }

    char pos[32];
    char status[TYPEWRT_DISPLAY_TEXT_COLUMNS + 1];
    snprintf(pos, sizeof(pos), " L%d C%d%s", ed->row + 1, ed->col + 1, ed->modified ? " *" : "");

    memset(status, ' ', TYPEWRT_DISPLAY_TEXT_COLUMNS);
    status[TYPEWRT_DISPLAY_TEXT_COLUMNS] = '\0';
    status[0] = ed->mode == NEXTVI_MODE_INSERT ? 'I' : 'N';
    status[1] = ' ';
    memcpy(&status[2], ed->status, MIN((int)strlen(ed->status), 20));
    int pos_len = MIN((int)strlen(pos), TYPEWRT_DISPLAY_TEXT_COLUMNS - 24);
    memcpy(&status[TYPEWRT_DISPLAY_TEXT_COLUMNS - pos_len], pos, pos_len);
    typewrt_display_draw_text_line(TYPEWRT_DISPLAY_STATUS_ROW, status, -1, -1, 0);
}

static void insert_char(NextviEditor *ed, char ch)
{
    int row = ed->row;
    int len = ed->lengths[row];
    ensure_text_capacity(ed, row, len + 2);
    memmove(&ed->lines[row][ed->col + 1], &ed->lines[row][ed->col], len - ed->col + 1);
    ed->lines[row][ed->col] = ch;
    ed->lengths[row]++;
    ed->col++;
    ed->preferred_col = ed->col;
    ed->modified = true;
}

static void split_line(NextviEditor *ed)
{
    int row = ed->row;
    int tail_len = ed->lengths[row] - ed->col;
    char *tail = xmalloc(tail_len + 1);
    memcpy(tail, &ed->lines[row][ed->col], tail_len);
    tail[tail_len] = '\0';
    ed->lines[row][ed->col] = '\0';
    ed->lengths[row] = ed->col;
    insert_line(ed, row + 1, tail, tail_len);
    ed->row++;
    ed->col = 0;
    ed->preferred_col = 0;
    ed->modified = true;
}

static void backspace(NextviEditor *ed)
{
    if (ed->col > 0) {
        int row = ed->row;
        int len = ed->lengths[row];
        memmove(&ed->lines[row][ed->col - 1], &ed->lines[row][ed->col], len - ed->col + 1);
        ed->col--;
        ed->lengths[row]--;
        ed->modified = true;
    } else if (ed->row > 0) {
        int prev = ed->row - 1;
        int prev_len = ed->lengths[prev];
        int cur_len = ed->lengths[ed->row];
        ensure_text_capacity(ed, prev, prev_len + cur_len + 1);
        memcpy(&ed->lines[prev][prev_len], ed->lines[ed->row], cur_len + 1);
        ed->lengths[prev] += cur_len;
        delete_line(ed, ed->row);
        ed->row = prev;
        ed->col = prev_len;
        ed->modified = true;
    }
    ed->preferred_col = ed->col;
}

static void delete_char(NextviEditor *ed)
{
    int row = ed->row;
    int len = ed->lengths[row];
    if (ed->col < len) {
        memmove(&ed->lines[row][ed->col], &ed->lines[row][ed->col + 1], len - ed->col);
        ed->lengths[row]--;
        ed->modified = true;
    } else if (row + 1 < ed->line_count) {
        int next_len = ed->lengths[row + 1];
        ensure_text_capacity(ed, row, len + next_len + 1);
        memcpy(&ed->lines[row][len], ed->lines[row + 1], next_len + 1);
        ed->lengths[row] += next_len;
        delete_line(ed, row + 1);
        ed->modified = true;
    }
}

static void move_vertical(NextviEditor *ed, int delta)
{
    ed->row += delta;
    clamp_cursor(ed);
    if (ed->col != ed->preferred_col) {
        ed->col = ed->preferred_col;
        clamp_cursor(ed);
    }
}

static bool event_to_key(uint8_t event, Virtual_Key *vk)
{
    bool keydown = (event & KEYDOWN_MASK);
    bool modifier = (event & MOD_MASK);

    if (modifier) {
        if (keydown) {
            KBD_MODS |= (event & KEY_MASK);
        } else {
            KBD_MODS &= ~(event & KEY_MASK);
        }
        return false;
    }

    if (!keydown) {
        return false;
    }

    int code = event & KEY_MASK;
    bool shifted = KBD_MODS & 1;
    *vk = shifted ? keymap_shift[code] : keymap[code];
    return true;
}

static int virtual_key_to_ascii(Virtual_Key vk)
{
    if (vk >= VKCHAROFFSET && vk <= VK_z) {
        int idx = vk - VKCHAROFFSET;
        if (unicodemap[idx] >= 0x20 && unicodemap[idx] <= 0x7e) {
            return unicodemap[idx];
        }
    }
    return 0;
}

static void handle_insert_key(NextviEditor *ed, Virtual_Key vk)
{
    switch (vk) {
    case VK_ESC:
        ed->mode = NEXTVI_MODE_NORMAL;
        editor_set_status(ed, "normal");
        if (ed->col > 0) {
            ed->col--;
        }
        break;
    case VK_ENTER:
        split_line(ed);
        break;
    case VK_BACKSPACE:
        backspace(ed);
        break;
    case VK_LEFT:
        if (ed->col > 0) {
            ed->col--;
        }
        ed->preferred_col = ed->col;
        break;
    case VK_RIGHT:
        if (ed->col < ed->lengths[ed->row]) {
            ed->col++;
        }
        ed->preferred_col = ed->col;
        break;
    case VK_UP:
        move_vertical(ed, -1);
        break;
    case VK_DOWN:
        move_vertical(ed, 1);
        break;
    default: {
        int ch = virtual_key_to_ascii(vk);
        if (ch) {
            insert_char(ed, (char)ch);
        }
        break;
    }
    }
}

static void open_line_below(NextviEditor *ed)
{
    insert_line(ed, ed->row + 1, line_new_empty(), 0);
    ed->row++;
    ed->col = 0;
    ed->preferred_col = 0;
    ed->mode = NEXTVI_MODE_INSERT;
    ed->modified = true;
    editor_set_status(ed, "insert");
}

static void handle_normal_key(NextviEditor *ed, Virtual_Key vk)
{
    int ch = virtual_key_to_ascii(vk);

    switch (vk) {
    case VK_LEFT:
        ch = 'h';
        break;
    case VK_DOWN:
        ch = 'j';
        break;
    case VK_UP:
        ch = 'k';
        break;
    case VK_RIGHT:
        ch = 'l';
        break;
    default:
        break;
    }

    switch (ch) {
    case 'h':
        if (ed->col > 0) {
            ed->col--;
        }
        ed->preferred_col = ed->col;
        break;
    case 'j':
        move_vertical(ed, 1);
        break;
    case 'k':
        move_vertical(ed, -1);
        break;
    case 'l':
        if (ed->col < ed->lengths[ed->row]) {
            ed->col++;
        }
        ed->preferred_col = ed->col;
        break;
    case '0':
        ed->col = 0;
        ed->preferred_col = ed->col;
        break;
    case '$':
        ed->col = ed->lengths[ed->row] ? ed->lengths[ed->row] - 1 : 0;
        ed->preferred_col = ed->col;
        break;
    case 'i':
        ed->mode = NEXTVI_MODE_INSERT;
        editor_set_status(ed, "insert");
        break;
    case 'a':
        if (ed->col < ed->lengths[ed->row]) {
            ed->col++;
        }
        ed->mode = NEXTVI_MODE_INSERT;
        editor_set_status(ed, "insert");
        break;
    case 'o':
        open_line_below(ed);
        break;
    case 'x':
        delete_char(ed);
        break;
    case 'd':
        delete_line(ed, ed->row);
        ed->modified = true;
        editor_set_status(ed, "line deleted");
        break;
    case 's':
        nextvi_storage_save(ed, NULL);
        break;
    case 'r':
        nextvi_storage_load(ed, NULL);
        break;
    default:
        break;
    }
}

void nextvi_esp_run(QueueHandle_t keyboard)
{
    NextviEditor editor;
    uint8_t event;
    Virtual_Key vk;

    editor_init(&editor);
    editor_draw(&editor);

    while (1) {
        if (xQueueReceive(keyboard, &event, pdMS_TO_TICKS(250)) == pdTRUE) {
            if (!event_to_key(event, &vk)) {
                editor_set_keyboard_debug_status(&editor, "raw", event, -1);
                editor_draw(&editor);
                continue;
            }

            if (editor.mode == NEXTVI_MODE_INSERT) {
                handle_insert_key(&editor, vk);
            } else {
                handle_normal_key(&editor, vk);
            }
            editor_set_keyboard_debug_status(&editor, "key", event, vk);
            editor_draw(&editor);
        } else {
            typewrt_keyboard_debug_t debug;
            typewrt_keyboard_debug_snapshot(&debug);
            snprintf(editor.status, sizeof(editor.status), "scan %lu p%lu r%lu last %02x",
                     (unsigned long)debug.scans,
                     (unsigned long)debug.press_events,
                     (unsigned long)debug.release_events,
                     debug.last_event);
            editor_draw(&editor);
        }
        vTaskDelay(1);
    }

    editor_free(&editor);
}
