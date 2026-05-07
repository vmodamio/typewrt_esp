#include "nextvi_esp.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "keyboard_input.h"
#include "typewrt_display.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi) (MIN(MAX((v), (lo)), (hi)))

#define NEXTVI_INITIAL_LINE_CAP 32
#define NEXTVI_INITIAL_LINE_LEN 64
#define NEXTVI_UNDO_DEPTH 16
#define NEXTVI_CMD_MAX 80
#define NEXTVI_SEARCH_MAX 80
#define NEXTVI_STATUS_MAX (TYPEWRT_DISPLAY_TEXT_COLUMNS + 1)
#define NEXTVI_CTRL(ch) ((ch) & 0x1f)
#define NEXTVI_HARD_WRAP_WIDTH TYPEWRT_DISPLAY_TEXT_COLUMNS
#define NEXTVI_HWBRK "\342\200\213"
#define NEXTVI_HWBRK_LEN 3
#define NEXTVI_STATUS_TIMEOUT_TICKS pdMS_TO_TICKS(3000)
#define NEXTVI_SPLASH_WIDTH TYPEWRT_DISPLAY_WIDTH
#define NEXTVI_SPLASH_HEIGHT 160
#define NEXTVI_SPLASH_STRIDE (NEXTVI_SPLASH_WIDTH / 8)
#define NEXTVI_SPLASH_CURSOR_ROW 13

typedef enum {
    NEXTVI_MODE_NORMAL,
    NEXTVI_MODE_INSERT,
    NEXTVI_MODE_REPLACE,
    NEXTVI_MODE_VISUAL,
    NEXTVI_MODE_VISUAL_LINE,
    NEXTVI_MODE_COMMAND,
    NEXTVI_MODE_SEARCH,
} NextviMode;

typedef enum {
    PROMPT_NONE,
    PROMPT_COMMAND,
    PROMPT_SEARCH_FORWARD,
    PROMPT_SEARCH_BACKWARD,
} PromptKind;

typedef enum {
    OP_NONE,
    OP_DELETE,
    OP_CHANGE,
    OP_YANK,
    OP_SHIFT_LEFT,
    OP_SHIFT_RIGHT,
    OP_CASE_SWAP,
    OP_CASE_LOWER,
    OP_CASE_UPPER,
} PendingOp;

typedef enum {
    MOTION_NONE,
    MOTION_CHAR,
    MOTION_LINE,
} MotionKind;

typedef struct {
    int row;
    int col;
} Cursor;

typedef struct {
    char **lines;
    int *lengths;
    int *caps;
    int line_count;
    int line_cap;
} BufferSnapshot;

typedef struct {
    int ascii;
    Virtual_Key vk;
    bool ctrl;
} EditorKey;

typedef struct {
    char **lines;
    int *lengths;
    int *caps;
    int line_count;
    int line_cap;
    int row;
    int col;
    int top;
    int left;
    int preferred_col;
    bool modified;
    bool splash_visible;
    bool splash_ready;
    bool status_visible;
    TickType_t status_until;
    NextviMode mode;
    NextviMode prev_mode;
    char status[NEXTVI_STATUS_MAX];

    int count;
    PendingOp pending_op;
    bool awaiting_g;
    bool awaiting_z;
    bool awaiting_mark;
    bool awaiting_mark_set;
    bool awaiting_mark_line;
    bool awaiting_find;
    bool awaiting_replace;
    int find_cmd;
    int last_find_cmd;
    char last_find_char;

    Cursor visual_anchor;
    Cursor marks[26];
    bool mark_set[26];

    char *yank;
    int yank_len;
    bool yank_linewise;

    char prompt[NEXTVI_CMD_MAX + 1];
    int prompt_len;
    PromptKind prompt_kind;
    char search[NEXTVI_SEARCH_MAX + 1];
    int search_dir;

    BufferSnapshot undo[NEXTVI_UNDO_DEPTH];
    int undo_len;
    BufferSnapshot redo[NEXTVI_UNDO_DEPTH];
    int redo_len;
    uint8_t *splash_bitmap;
} NextviEditor;

static void editor_set_status(NextviEditor *ed, const char *status);
static void editor_show_status(NextviEditor *ed, const char *status);

static bool nextvi_storage_load(NextviEditor *ed, const char *path)
{
    (void)path;
    editor_show_status(ed, "load waits for SD card");
    return false;
}

static bool nextvi_storage_save(NextviEditor *ed, const char *path)
{
    (void)path;
    editor_show_status(ed, "save waits for SD card");
    return false;
}

static void die_oom(void)
{
    printf("nextvi: out of memory\n");
    abort();
}

static void *xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p) {
        die_oom();
    }
    return p;
}

static void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (!p) {
        die_oom();
    }
    return p;
}

static char *xstrndup(const char *s, int len)
{
    char *out = xmalloc(len + 1);
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static char *line_new_empty(void)
{
    char *line = xmalloc(NEXTVI_INITIAL_LINE_LEN);
    line[0] = '\0';
    return line;
}

static bool line_forced(const char *line, int len)
{
    return len >= NEXTVI_HWBRK_LEN && !memcmp(line, NEXTVI_HWBRK, NEXTVI_HWBRK_LEN);
}

static int line_body_start(const char *line, int len)
{
    return line_forced(line, len) ? NEXTVI_HWBRK_LEN : 0;
}

static void editor_set_status(NextviEditor *ed, const char *status)
{
    snprintf(ed->status, sizeof(ed->status), "%s", status);
}

static void editor_show_status(NextviEditor *ed, const char *status)
{
    editor_set_status(ed, status);
    ed->status_visible = true;
    ed->status_until = xTaskGetTickCount() + NEXTVI_STATUS_TIMEOUT_TICKS;
}

static bool editor_hide_expired_status(NextviEditor *ed)
{
    if (!ed->status_visible) {
        return false;
    }
    if (ed->mode == NEXTVI_MODE_COMMAND || ed->mode == NEXTVI_MODE_SEARCH) {
        return false;
    }
    if ((int32_t)(xTaskGetTickCount() - ed->status_until) < 0) {
        return false;
    }
    ed->status_visible = false;
    return true;
}

static void splash_set_pixel(NextviEditor *ed, int x, int y, bool black)
{
    if (x < 0 || x >= NEXTVI_SPLASH_WIDTH || y < 0 || y >= NEXTVI_SPLASH_HEIGHT) {
        return;
    }
    uint8_t *byte = &ed->splash_bitmap[y * NEXTVI_SPLASH_STRIDE + (x >> 3)];
    uint8_t mask = 0x80 >> (x & 7);
    if (black) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
}

static const uint16_t splash_typewrt_spans[][3] = {
    {63, 55, 27}, {64, 54, 29}, {65, 54, 29}, {65, 251, 6},
    {66, 54, 29}, {66, 249, 8}, {67, 54, 29}, {67, 248, 9},
    {68, 54, 29}, {68, 247, 10}, {69, 55, 28}, {69, 245, 12},
    {70, 61, 16}, {70, 86, 12}, {70, 107, 6}, {70, 117, 9},
    {70, 131, 7}, {70, 156, 9}, {70, 174, 12}, {70, 194, 7},
    {70, 209, 6}, {70, 219, 10}, {70, 234, 5}, {70, 244, 19},
    {71, 61, 16}, {71, 86, 13}, {71, 107, 6}, {71, 116, 11},
    {71, 129, 11}, {71, 153, 15}, {71, 174, 12}, {71, 193, 9},
    {71, 209, 6}, {71, 218, 12}, {71, 232, 8}, {71, 242, 21},
    {72, 61, 16}, {72, 86, 13}, {72, 106, 7}, {72, 116, 25},
    {72, 151, 19}, {72, 174, 13}, {72, 192, 10}, {72, 208, 7},
    {72, 218, 12}, {72, 231, 9}, {72, 242, 21}, {73, 61, 16},
    {73, 87, 13}, {73, 106, 6}, {73, 116, 25}, {73, 150, 21},
    {73, 175, 12}, {73, 192, 11}, {73, 208, 6}, {73, 218, 22},
    {73, 242, 21}, {74, 61, 16}, {74, 87, 13}, {74, 105, 7},
    {74, 116, 26}, {74, 149, 11}, {74, 163, 8}, {74, 175, 12},
    {74, 191, 12}, {74, 208, 6}, {74, 218, 22}, {74, 243, 20},
    {75, 61, 16}, {75, 88, 13}, {75, 105, 6}, {75, 116, 26},
    {75, 148, 11}, {75, 164, 8}, {75, 175, 12}, {75, 191, 12},
    {75, 207, 7}, {75, 218, 22}, {75, 245, 12}, {76, 61, 16},
    {76, 88, 13}, {76, 105, 6}, {76, 116, 27}, {76, 148, 11},
    {76, 164, 8}, {76, 176, 12}, {76, 191, 13}, {76, 207, 6},
    {76, 218, 22}, {76, 245, 12}, {77, 61, 16}, {77, 89, 13},
    {77, 104, 7}, {77, 116, 12}, {77, 131, 12}, {77, 148, 11},
    {77, 163, 9}, {77, 176, 12}, {77, 190, 14}, {77, 206, 7},
    {77, 218, 13}, {77, 234, 5}, {77, 245, 12}, {78, 61, 16},
    {78, 89, 13}, {78, 104, 6}, {78, 116, 12}, {78, 132, 11},
    {78, 147, 25}, {78, 176, 12}, {78, 190, 14}, {78, 206, 7},
    {78, 218, 12}, {78, 236, 2}, {78, 245, 12}, {79, 61, 16},
    {79, 90, 20}, {79, 116, 12}, {79, 132, 11}, {79, 147, 24},
    {79, 177, 35}, {79, 218, 12}, {79, 245, 12}, {80, 61, 16},
    {80, 90, 19}, {80, 116, 12}, {80, 132, 11}, {80, 147, 24},
    {80, 177, 35}, {80, 218, 12}, {80, 245, 12}, {81, 61, 16},
    {81, 91, 18}, {81, 116, 12}, {81, 132, 11}, {81, 147, 22},
    {81, 178, 33}, {81, 218, 12}, {81, 245, 12}, {82, 61, 16},
    {82, 91, 17}, {82, 116, 12}, {82, 132, 11}, {82, 147, 12},
    {82, 178, 33}, {82, 218, 12}, {82, 245, 12}, {83, 61, 16},
    {83, 92, 16}, {83, 116, 13}, {83, 131, 12}, {83, 147, 12},
    {83, 179, 15}, {83, 195, 16}, {83, 218, 12}, {83, 245, 14},
    {83, 262, 2}, {84, 61, 16}, {84, 92, 15}, {84, 116, 26},
    {84, 148, 12}, {84, 168, 3}, {84, 179, 15}, {84, 196, 14},
    {84, 218, 12}, {84, 246, 19}, {85, 61, 16}, {85, 93, 14},
    {85, 116, 26}, {85, 148, 24}, {85, 179, 14}, {85, 196, 14},
    {85, 218, 12}, {85, 246, 19}, {86, 61, 16}, {86, 94, 12},
    {86, 116, 26}, {86, 149, 23}, {86, 180, 13}, {86, 196, 13},
    {86, 218, 12}, {86, 246, 19}, {87, 61, 16}, {87, 94, 12},
    {87, 116, 25}, {87, 150, 22}, {87, 180, 12}, {87, 197, 12},
    {87, 218, 12}, {87, 247, 18}, {88, 61, 16}, {88, 95, 10},
    {88, 116, 12}, {88, 129, 11}, {88, 152, 19}, {88, 181, 11},
    {88, 197, 11}, {88, 218, 12}, {88, 249, 15}, {89, 62, 14},
    {89, 95, 10}, {89, 116, 12}, {89, 130, 8}, {89, 154, 14},
    {89, 182, 9}, {89, 199, 8}, {89, 219, 10}, {89, 251, 11},
    {90, 94, 10}, {90, 116, 12}, {91, 89, 15}, {91, 116, 12},
    {92, 88, 15}, {92, 116, 12}, {93, 88, 14}, {93, 116, 11},
    {94, 88, 13}, {94, 116, 11}, {95, 88, 12}, {95, 116, 11},
    {96, 89, 8}, {96, 117, 10},
};

static void splash_prepare(NextviEditor *ed)
{
    if (ed->splash_ready) {
        return;
    }

    memset(ed->splash_bitmap, 0, NEXTVI_SPLASH_STRIDE * NEXTVI_SPLASH_HEIGHT);
    for (int i = 0; i < (int)(sizeof(splash_typewrt_spans) / sizeof(splash_typewrt_spans[0])); i++) {
        int y = splash_typewrt_spans[i][0];
        int x = splash_typewrt_spans[i][1];
        int len = splash_typewrt_spans[i][2];
        for (int dx = 0; dx < len; dx++) {
            splash_set_pixel(ed, x + dx, y, true);
        }
    }

    ed->splash_ready = true;
}

static void snapshot_free(BufferSnapshot *snap)
{
    for (int i = 0; i < snap->line_count; i++) {
        free(snap->lines[i]);
    }
    free(snap->lines);
    free(snap->lengths);
    free(snap->caps);
    memset(snap, 0, sizeof(*snap));
}

static BufferSnapshot snapshot_make(const NextviEditor *ed)
{
    BufferSnapshot snap = {
        .line_count = ed->line_count,
        .line_cap = ed->line_count,
    };

    snap.lines = xmalloc(sizeof(snap.lines[0]) * snap.line_cap);
    snap.lengths = xmalloc(sizeof(snap.lengths[0]) * snap.line_cap);
    snap.caps = xmalloc(sizeof(snap.caps[0]) * snap.line_cap);

    for (int i = 0; i < snap.line_count; i++) {
        snap.lengths[i] = ed->lengths[i];
        snap.caps[i] = ed->lengths[i] + 1;
        snap.lines[i] = xstrndup(ed->lines[i], ed->lengths[i]);
    }

    return snap;
}

static void snapshot_restore(NextviEditor *ed, BufferSnapshot *snap)
{
    for (int i = 0; i < ed->line_count; i++) {
        free(ed->lines[i]);
    }
    free(ed->lines);
    free(ed->lengths);
    free(ed->caps);

    ed->line_count = snap->line_count;
    ed->line_cap = snap->line_cap;
    ed->lines = snap->lines;
    ed->lengths = snap->lengths;
    ed->caps = snap->caps;
    memset(snap, 0, sizeof(*snap));
}

static void history_clear(BufferSnapshot *hist, int *len)
{
    for (int i = 0; i < *len; i++) {
        snapshot_free(&hist[i]);
    }
    *len = 0;
}

static void history_push(BufferSnapshot *hist, int *len, BufferSnapshot snap)
{
    if (*len == NEXTVI_UNDO_DEPTH) {
        snapshot_free(&hist[0]);
        memmove(&hist[0], &hist[1], sizeof(hist[0]) * (NEXTVI_UNDO_DEPTH - 1));
        *len -= 1;
    }
    hist[(*len)++] = snap;
}

static void begin_change(NextviEditor *ed)
{
    history_push(ed->undo, &ed->undo_len, snapshot_make(ed));
    history_clear(ed->redo, &ed->redo_len);
}

static void editor_init(NextviEditor *ed)
{
    memset(ed, 0, sizeof(*ed));
    ed->line_cap = NEXTVI_INITIAL_LINE_CAP;
    ed->lines = xmalloc(sizeof(ed->lines[0]) * ed->line_cap);
    ed->lengths = xmalloc(sizeof(ed->lengths[0]) * ed->line_cap);
    ed->caps = xmalloc(sizeof(ed->caps[0]) * ed->line_cap);
    ed->splash_bitmap = xmalloc(NEXTVI_SPLASH_STRIDE * NEXTVI_SPLASH_HEIGHT);
    ed->lines[0] = line_new_empty();
    ed->lengths[0] = 0;
    ed->caps[0] = NEXTVI_INITIAL_LINE_LEN;
    ed->line_count = 1;
    ed->mode = NEXTVI_MODE_NORMAL;
    ed->search_dir = 1;
    ed->splash_visible = true;
    ed->status_visible = true;
    ed->status_until = portMAX_DELAY;
    editor_set_status(ed, "normal");
}

static void editor_free(NextviEditor *ed)
{
    for (int i = 0; i < ed->line_count; i++) {
        free(ed->lines[i]);
    }
    free(ed->lines);
    free(ed->lengths);
    free(ed->caps);
    free(ed->splash_bitmap);
    free(ed->yank);
    history_clear(ed->undo, &ed->undo_len);
    history_clear(ed->redo, &ed->redo_len);
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
    ed->caps = xrealloc(ed->caps, sizeof(ed->caps[0]) * ed->line_cap);
}

static void ensure_text_capacity(NextviEditor *ed, int row, int needed)
{
    if (needed <= ed->caps[row]) {
        return;
    }
    while (ed->caps[row] < needed) {
        ed->caps[row] *= 2;
    }
    ed->lines[row] = xrealloc(ed->lines[row], ed->caps[row]);
}

static void insert_line_raw(NextviEditor *ed, int row, char *line, int len, int cap)
{
    if (row < 0) {
        row = 0;
    } else if (row > ed->line_count) {
        row = ed->line_count;
    }
    ensure_line_capacity(ed, ed->line_count + 1);
    memmove(&ed->lines[row + 1], &ed->lines[row], sizeof(ed->lines[0]) * (ed->line_count - row));
    memmove(&ed->lengths[row + 1], &ed->lengths[row], sizeof(ed->lengths[0]) * (ed->line_count - row));
    memmove(&ed->caps[row + 1], &ed->caps[row], sizeof(ed->caps[0]) * (ed->line_count - row));
    ed->lines[row] = line;
    ed->lengths[row] = len;
    ed->caps[row] = MAX(cap, len + 1);
    ed->line_count++;
}

static void delete_line_raw(NextviEditor *ed, int row)
{
    if (ed->line_count == 1) {
        ed->lines[0][0] = '\0';
        ed->lengths[0] = 0;
        return;
    }

    free(ed->lines[row]);
    memmove(&ed->lines[row], &ed->lines[row + 1], sizeof(ed->lines[0]) * (ed->line_count - row - 1));
    memmove(&ed->lengths[row], &ed->lengths[row + 1], sizeof(ed->lengths[0]) * (ed->line_count - row - 1));
    memmove(&ed->caps[row], &ed->caps[row + 1], sizeof(ed->caps[0]) * (ed->line_count - row - 1));
    ed->line_count--;
}

static int line_last_col(NextviEditor *ed, int row)
{
    int start = line_body_start(ed->lines[row], ed->lengths[row]);
    return ed->lengths[row] > start ? ed->lengths[row] - 1 : start;
}

static void clamp_cursor(NextviEditor *ed)
{
    ed->row = CLAMP(ed->row, 0, ed->line_count - 1);
    int start = line_body_start(ed->lines[ed->row], ed->lengths[ed->row]);
    ed->col = CLAMP(ed->col, start, ed->lengths[ed->row]);
    if (ed->mode != NEXTVI_MODE_INSERT && ed->mode != NEXTVI_MODE_REPLACE) {
        ed->col = MIN(ed->col, line_last_col(ed, ed->row));
    }
}

static void keep_cursor_visible(NextviEditor *ed, int text_rows)
{
    int visible_col = ed->col - line_body_start(ed->lines[ed->row], ed->lengths[ed->row]);
    if (ed->row < ed->top) {
        ed->top = ed->row;
    } else if (ed->row >= ed->top + text_rows) {
        ed->top = ed->row - text_rows + 1;
    }

    if (visible_col < ed->left) {
        ed->left = visible_col;
    } else if (visible_col >= ed->left + TYPEWRT_DISPLAY_TEXT_COLUMNS) {
        ed->left = visible_col - TYPEWRT_DISPLAY_TEXT_COLUMNS + 1;
    }
}

static bool cursor_before(Cursor a, Cursor b)
{
    return a.row < b.row || (a.row == b.row && a.col < b.col);
}

static void ordered_region(Cursor a, Cursor b, Cursor *beg, Cursor *end)
{
    if (cursor_before(b, a)) {
        *beg = b;
        *end = a;
    } else {
        *beg = a;
        *end = b;
    }
}

static bool is_selected(NextviEditor *ed, int row, int col)
{
    if (ed->mode != NEXTVI_MODE_VISUAL && ed->mode != NEXTVI_MODE_VISUAL_LINE) {
        return false;
    }
    if (ed->mode == NEXTVI_MODE_VISUAL_LINE) {
        int a = MIN(ed->visual_anchor.row, ed->row);
        int b = MAX(ed->visual_anchor.row, ed->row);
        return row >= a && row <= b;
    }

    Cursor beg;
    Cursor end;
    ordered_region(ed->visual_anchor, (Cursor){ed->row, ed->col}, &beg, &end);
    if (row < beg.row || row > end.row) {
        return false;
    }
    if (beg.row == end.row) {
        return col >= beg.col && col <= end.col;
    }
    if (row == beg.row) {
        return col >= beg.col;
    }
    if (row == end.row) {
        return col <= end.col;
    }
    return true;
}

static char display_char(NextviEditor *ed, int row, int col, char ch)
{
    if (is_selected(ed, row, col)) {
        return ch >= 'a' && ch <= 'z' ? (char)toupper((unsigned char)ch) : ch;
    }
    return ch;
}

static const char *mode_name(NextviMode mode)
{
    switch (mode) {
    case NEXTVI_MODE_INSERT:
        return "I";
    case NEXTVI_MODE_REPLACE:
        return "R";
    case NEXTVI_MODE_VISUAL:
        return "V";
    case NEXTVI_MODE_VISUAL_LINE:
        return "VL";
    case NEXTVI_MODE_COMMAND:
        return ":";
    case NEXTVI_MODE_SEARCH:
        return "/";
    case NEXTVI_MODE_NORMAL:
    default:
        return "N";
    }
}

static void editor_draw(NextviEditor *ed)
{
    clamp_cursor(ed);
    bool prompt_visible = ed->mode == NEXTVI_MODE_COMMAND || ed->mode == NEXTVI_MODE_SEARCH;
    bool status_visible = prompt_visible || ed->status_visible;

    if (ed->splash_visible) {
        splash_prepare(ed);
        typewrt_display_draw_bitmap(0, 0, NEXTVI_SPLASH_WIDTH, NEXTVI_SPLASH_HEIGHT,
                                    ed->splash_bitmap, NEXTVI_SPLASH_STRIDE);

        for (int row = NEXTVI_SPLASH_HEIGHT / TYPEWRT_DISPLAY_GLYPH_HEIGHT;
             row < NEXTVI_SPLASH_CURSOR_ROW; row++) {
            typewrt_display_draw_text_line(row, "", 0, -1, 0);
        }

        char rowbuf[TYPEWRT_DISPLAY_TEXT_COLUMNS + 1];
        int out_len = 0;
        int start = line_body_start(ed->lines[ed->row], ed->lengths[ed->row]);
        for (int i = 0; i < TYPEWRT_DISPLAY_TEXT_COLUMNS; i++) {
            int col = start + ed->left + i;
            if (col < ed->lengths[ed->row]) {
                rowbuf[out_len++] = ed->lines[ed->row][col];
            } else {
                break;
            }
        }
        rowbuf[out_len] = '\0';
        typewrt_display_draw_text_line(NEXTVI_SPLASH_CURSOR_ROW, rowbuf, out_len,
                                       ed->col - start - ed->left, 1);

        char status[TYPEWRT_DISPLAY_TEXT_COLUMNS + 1];
        memset(status, ' ', TYPEWRT_DISPLAY_TEXT_COLUMNS);
        status[TYPEWRT_DISPLAY_TEXT_COLUMNS] = '\0';
        if (prompt_visible) {
            const char prefix = ed->prompt_kind == PROMPT_SEARCH_BACKWARD ? '?' :
                                ed->prompt_kind == PROMPT_COMMAND ? ':' : '/';
            status[0] = prefix;
            memcpy(&status[1], ed->prompt, MIN(ed->prompt_len, TYPEWRT_DISPLAY_TEXT_COLUMNS - 1));
        } else {
            const char *mode = mode_name(ed->mode);
            int mode_len = MIN((int)strlen(mode), TYPEWRT_DISPLAY_TEXT_COLUMNS);
            memcpy(status, mode, mode_len);
            if (mode_len < TYPEWRT_DISPLAY_TEXT_COLUMNS) {
                status[mode_len++] = ' ';
            }
            memcpy(&status[mode_len], ed->status,
                   MIN((int)strlen(ed->status), TYPEWRT_DISPLAY_TEXT_COLUMNS - mode_len));
        }
        typewrt_display_draw_text_line(TYPEWRT_DISPLAY_STATUS_ROW, status, -1, -1, 0);
        return;
    }

    int text_rows = status_visible ? TYPEWRT_DISPLAY_TEXT_ROWS : TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS;

    if (ed->row < ed->top) {
        ed->top = ed->row;
    } else if (ed->row >= ed->top + text_rows) {
        ed->top = ed->row - text_rows + 1;
    }
    keep_cursor_visible(ed, text_rows);

    char rowbuf[TYPEWRT_DISPLAY_TEXT_COLUMNS + 1];
    for (int screen_row = 0; screen_row < text_rows; screen_row++) {
        int buffer_row = ed->top + screen_row;
        if (buffer_row < ed->line_count) {
            int out_len = 0;
            int start = line_body_start(ed->lines[buffer_row], ed->lengths[buffer_row]);
            for (int i = 0; i < TYPEWRT_DISPLAY_TEXT_COLUMNS; i++) {
                int col = start + ed->left + i;
                if (col < ed->lengths[buffer_row]) {
                    rowbuf[out_len++] = display_char(ed, buffer_row, col, ed->lines[buffer_row][col]);
                } else {
                    break;
                }
            }
            rowbuf[out_len] = '\0';
            int cursor_col = buffer_row == ed->row ? ed->col - start - ed->left : -1;
            typewrt_display_draw_text_line(screen_row, rowbuf, out_len, cursor_col, buffer_row == ed->row);
        } else {
            typewrt_display_draw_text_line(screen_row, "~", 1, -1, 0);
        }
    }

    if (!status_visible) {
        return;
    }

    char pos[32];
    char status[TYPEWRT_DISPLAY_TEXT_COLUMNS + 1];
    snprintf(pos, sizeof(pos), " L%d C%d%s", ed->row + 1, ed->col + 1, ed->modified ? " *" : "");
    memset(status, ' ', TYPEWRT_DISPLAY_TEXT_COLUMNS);
    status[TYPEWRT_DISPLAY_TEXT_COLUMNS] = '\0';
    snprintf(status, sizeof(status), "%s ", mode_name(ed->mode));

    if (ed->mode == NEXTVI_MODE_COMMAND || ed->mode == NEXTVI_MODE_SEARCH) {
        const char prefix = ed->prompt_kind == PROMPT_SEARCH_BACKWARD ? '?' :
                            ed->prompt_kind == PROMPT_COMMAND ? ':' : '/';
        status[0] = prefix;
        memcpy(&status[1], ed->prompt, MIN(ed->prompt_len, TYPEWRT_DISPLAY_TEXT_COLUMNS - 1));
    } else {
        int start = (int)strlen(status);
        memcpy(&status[start], ed->status, MIN((int)strlen(ed->status), TYPEWRT_DISPLAY_TEXT_COLUMNS - start));
        int pos_len = MIN((int)strlen(pos), TYPEWRT_DISPLAY_TEXT_COLUMNS);
        memcpy(&status[TYPEWRT_DISPLAY_TEXT_COLUMNS - pos_len], pos, pos_len);
    }
    typewrt_display_draw_text_line(TYPEWRT_DISPLAY_STATUS_ROW, status, -1, -1, 0);
}

static void set_mode(NextviEditor *ed, NextviMode mode, const char *status)
{
    ed->mode = mode;
    if (status) {
        editor_set_status(ed, status);
    }
}

static void splash_begin_edit(NextviEditor *ed)
{
    if (!ed->splash_visible) {
        return;
    }
    ed->splash_visible = false;
    ed->status_visible = false;
    ed->top = 0;
    ed->left = 0;
    ed->row = 0;
    ed->col = 0;
    ed->preferred_col = 0;
}

static int command_count(NextviEditor *ed)
{
    int count = ed->count ? ed->count : 1;
    ed->count = 0;
    return count;
}

static void clear_pending(NextviEditor *ed)
{
    ed->count = 0;
    ed->pending_op = OP_NONE;
    ed->awaiting_g = false;
    ed->awaiting_z = false;
    ed->awaiting_mark = false;
    ed->awaiting_mark_set = false;
    ed->awaiting_mark_line = false;
    ed->awaiting_find = false;
    ed->awaiting_replace = false;
}

static void insert_char_raw(NextviEditor *ed, char ch)
{
    int row = ed->row;
    int len = ed->lengths[row];
    ensure_text_capacity(ed, row, len + 2);
    memmove(&ed->lines[row][ed->col + 1], &ed->lines[row][ed->col], len - ed->col + 1);
    ed->lines[row][ed->col] = ch;
    ed->lengths[row]++;
    ed->col++;
    ed->preferred_col = ed->col;
}

static void split_line_raw(NextviEditor *ed)
{
    int row = ed->row;
    int tail_len = ed->lengths[row] - ed->col;
    char *tail = xmalloc(MAX(NEXTVI_INITIAL_LINE_LEN, tail_len + 1));
    memcpy(tail, &ed->lines[row][ed->col], tail_len);
    tail[tail_len] = '\0';
    ed->lines[row][ed->col] = '\0';
    ed->lengths[row] = ed->col;
    insert_line_raw(ed, row + 1, tail, tail_len, MAX(NEXTVI_INITIAL_LINE_LEN, tail_len + 1));
    ed->row++;
    ed->col = 0;
    ed->preferred_col = 0;
}

static void join_with_next_raw(NextviEditor *ed, bool with_space)
{
    if (ed->row + 1 >= ed->line_count) {
        return;
    }
    int row = ed->row;
    int len = ed->lengths[row];
    int next_len = ed->lengths[row + 1];
    int add_space = with_space && len && next_len;
    ensure_text_capacity(ed, row, len + add_space + next_len + 1);
    if (add_space) {
        ed->lines[row][len++] = ' ';
    }
    memcpy(&ed->lines[row][len], ed->lines[row + 1], next_len + 1);
    ed->lengths[row] = len + next_len;
    delete_line_raw(ed, row + 1);
    ed->col = MIN(ed->col, line_last_col(ed, row));
}

static void backspace_raw(NextviEditor *ed)
{
    if (ed->col > 0) {
        int row = ed->row;
        int len = ed->lengths[row];
        memmove(&ed->lines[row][ed->col - 1], &ed->lines[row][ed->col], len - ed->col + 1);
        ed->col--;
        ed->lengths[row]--;
    } else if (ed->row > 0) {
        ed->row--;
        ed->col = ed->lengths[ed->row];
        join_with_next_raw(ed, false);
    }
    ed->preferred_col = ed->col;
}

static void delete_char_raw(NextviEditor *ed)
{
    int row = ed->row;
    int len = ed->lengths[row];
    if (ed->col < len) {
        memmove(&ed->lines[row][ed->col], &ed->lines[row][ed->col + 1], len - ed->col);
        ed->lengths[row]--;
    } else if (row + 1 < ed->line_count) {
        join_with_next_raw(ed, false);
    }
}

static bool hardwrap_break(const char *text, int len, int width, int *end, int *next)
{
    int cut = 0;
    int br = -1;

    if (width <= 0 || len <= width) {
        return false;
    }

    while (cut < len && cut < width) {
        cut++;
    }

    if (cut < len && isspace((unsigned char)text[cut])) {
        br = cut + 1;
    } else {
        for (int i = cut; i > 0; i--) {
            unsigned char ch = (unsigned char)text[i - 1];
            if (isspace(ch) || strchr(",.;:!?)]}>/-", ch)) {
                br = i;
                break;
            }
        }
    }

    if (br > 0) {
        *end = br;
        *next = br;
        if (isspace((unsigned char)text[br - 1])) {
            *end = br - 1;
            while (*next < len && isspace((unsigned char)text[*next])) {
                (*next)++;
            }
        }
    } else {
        *end = cut;
        *next = cut;
    }

    if (*end <= 0) {
        *end = cut > 0 ? cut : 1;
    }
    if (*next <= *end) {
        *next = *end;
    }
    return true;
}

static void hardwrap_append(char **buf, int *len, int *cap, const char *s, int n)
{
    if (*len + n + 1 > *cap) {
        while (*len + n + 1 > *cap) {
            *cap *= 2;
        }
        *buf = xrealloc(*buf, *cap);
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

static char **hardwrap_emit(const char *text, int len, int cursor, int *line_count, int *cursor_line, int *cursor_col)
{
    int cap = 4;
    int count = 0;
    int chars = 0;
    char **out = xmalloc(sizeof(out[0]) * cap);

    *cursor_line = -1;
    *cursor_col = 0;

    while (chars < len || count == 0) {
        int remaining = len - chars;
        int end = remaining;
        int next = remaining;
        if (remaining > 0) {
            hardwrap_break(text + chars, remaining, NEXTVI_HARD_WRAP_WIDTH, &end, &next);
        }

        bool continuation = count > 0;
        int line_len = (continuation ? NEXTVI_HWBRK_LEN : 0) + end;
        int line_cap = MAX(NEXTVI_INITIAL_LINE_LEN, line_len + 1);
        char *line = xmalloc(line_cap);
        int pos = 0;
        if (continuation) {
            memcpy(line, NEXTVI_HWBRK, NEXTVI_HWBRK_LEN);
            pos += NEXTVI_HWBRK_LEN;
        }
        if (end > 0) {
            memcpy(line + pos, text + chars, end);
            pos += end;
        }
        line[pos] = '\0';

        if (count == cap) {
            cap *= 2;
            out = xrealloc(out, sizeof(out[0]) * cap);
        }
        out[count] = line;

        if (*cursor_line < 0 && (cursor < chars + next || chars + next >= len)) {
            *cursor_line = count;
            *cursor_col = (continuation ? NEXTVI_HWBRK_LEN : 0) + CLAMP(cursor - chars, 0, end);
        }

        count++;
        if (chars + next >= len) {
            break;
        }
        chars += next;
    }

    *line_count = count;
    if (*cursor_line < 0) {
        *cursor_line = count - 1;
        *cursor_col = (line_forced(out[count - 1], (int)strlen(out[count - 1])) ? NEXTVI_HWBRK_LEN : 0) +
                      (int)strlen(out[count - 1]);
    }
    return out;
}

static bool hardwrap_reflow(NextviEditor *ed, int row)
{
    if (NEXTVI_HARD_WRAP_WIDTH <= 0 || row < 0 || row >= ed->line_count) {
        return false;
    }

    int beg = row;
    while (beg > 0 && line_forced(ed->lines[beg], ed->lengths[beg])) {
        beg--;
    }

    int end = beg + 1;
    while (end < ed->line_count && line_forced(ed->lines[end], ed->lengths[end])) {
        end++;
    }

    int txt_cap = NEXTVI_INITIAL_LINE_LEN;
    int txt_len = 0;
    int cursor = 0;
    bool has_cursor = false;
    char *txt = xmalloc(txt_cap);
    txt[0] = '\0';

    for (int r = beg; r < end; r++) {
        int body = line_body_start(ed->lines[r], ed->lengths[r]);
        int skip = body;
        while (body && skip < ed->lengths[r] && isspace((unsigned char)ed->lines[r][skip])) {
            skip++;
        }
        bool sep = r > beg && txt_len > 0 && txt[txt_len - 1] != ' ' && skip < ed->lengths[r];

        if (r == ed->row) {
            has_cursor = true;
            cursor = txt_len + (sep ? 1 : 0) + CLAMP(ed->col - skip, 0, ed->lengths[r] - skip);
        }
        if (sep) {
            hardwrap_append(&txt, &txt_len, &txt_cap, " ", 1);
        }
        hardwrap_append(&txt, &txt_len, &txt_cap, ed->lines[r] + skip, ed->lengths[r] - skip);
    }

    if (end == beg + 1 && txt_len <= NEXTVI_HARD_WRAP_WIDTH) {
        free(txt);
        return false;
    }

    int new_count = 0;
    int cursor_line = 0;
    int cursor_col = 0;
    char **new_lines = hardwrap_emit(txt, txt_len, cursor, &new_count, &cursor_line, &cursor_col);
    int old_count = end - beg;

    if (old_count == ed->line_count) {
        for (int r = 0; r < ed->line_count; r++) {
            free(ed->lines[r]);
        }
        ed->line_count = 0;
    } else {
        for (int r = end - 1; r >= beg; r--) {
            delete_line_raw(ed, r);
        }
    }
    for (int i = 0; i < new_count; i++) {
        int len = (int)strlen(new_lines[i]);
        insert_line_raw(ed, beg + i, new_lines[i], len, MAX(NEXTVI_INITIAL_LINE_LEN, len + 1));
    }

    if (has_cursor) {
        ed->row = beg + cursor_line;
        ed->col = cursor_col;
    } else if (ed->row >= end) {
        ed->row += new_count - old_count;
    }
    ed->preferred_col = ed->col;

    free(new_lines);
    free(txt);
    return true;
}

static void hardwrap_all(NextviEditor *ed)
{
    for (int r = 0; r < ed->line_count; r++) {
        hardwrap_reflow(ed, r);
    }
}

static void move_vertical(NextviEditor *ed, int delta)
{
    ed->row = CLAMP(ed->row + delta, 0, ed->line_count - 1);
    ed->col = CLAMP(ed->preferred_col, 0, ed->lengths[ed->row]);
    if (ed->mode == NEXTVI_MODE_NORMAL || ed->mode == NEXTVI_MODE_VISUAL || ed->mode == NEXTVI_MODE_VISUAL_LINE) {
        ed->col = MIN(ed->col, line_last_col(ed, ed->row));
    }
}

static int first_nonblank(NextviEditor *ed, int row)
{
    int col = line_body_start(ed->lines[row], ed->lengths[row]);
    while (col < ed->lengths[row] && isspace((unsigned char)ed->lines[row][col])) {
        col++;
    }
    return MIN(col, line_last_col(ed, row));
}

static bool word_char(int ch, bool big)
{
    if (big) {
        return !isspace((unsigned char)ch);
    }
    return isalnum((unsigned char)ch) || ch == '_';
}

static void motion_word_forward(NextviEditor *ed, int count, bool big, bool end)
{
    while (count-- > 0) {
        int row = ed->row;
        int col = ed->col;
        bool seen_word = false;
        while (row < ed->line_count) {
            int len = ed->lengths[row];
            while (++col < len) {
                bool is_word = word_char(ed->lines[row][col], big);
                if (end) {
                    if (seen_word && (!is_word || col == len - 1)) {
                        ed->row = row;
                        ed->col = is_word && col == len - 1 ? col : col - 1;
                        ed->preferred_col = ed->col;
                        return;
                    }
                    seen_word |= is_word;
                } else if (is_word && (col == 0 || !word_char(ed->lines[row][col - 1], big))) {
                    ed->row = row;
                    ed->col = col;
                    ed->preferred_col = col;
                    return;
                }
            }
            row++;
            col = -1;
        }
        ed->row = ed->line_count - 1;
        ed->col = line_last_col(ed, ed->row);
    }
    ed->preferred_col = ed->col;
}

static void motion_word_backward(NextviEditor *ed, int count, bool big)
{
    while (count-- > 0) {
        int row = ed->row;
        int col = ed->col;
        while (row >= 0) {
            while (--col >= 0) {
                if (word_char(ed->lines[row][col], big) &&
                        (col == 0 || !word_char(ed->lines[row][col - 1], big))) {
                    ed->row = row;
                    ed->col = col;
                    ed->preferred_col = col;
                    return;
                }
            }
            row--;
            if (row >= 0) {
                col = ed->lengths[row];
            }
        }
        ed->row = 0;
        ed->col = 0;
    }
    ed->preferred_col = ed->col;
}

static void move_to_line(NextviEditor *ed, int row)
{
    ed->row = CLAMP(row, 0, ed->line_count - 1);
    ed->col = first_nonblank(ed, ed->row);
    ed->preferred_col = ed->col;
}

static bool find_char_on_line(NextviEditor *ed, int cmd, char ch, int count)
{
    int dir = (cmd == 'f' || cmd == 't') ? 1 : -1;
    int col = ed->col;
    while (count > 0) {
        col += dir;
        bool found = false;
        while (col >= 0 && col < ed->lengths[ed->row]) {
            if (ed->lines[ed->row][col] == ch) {
                found = true;
                break;
            }
            col += dir;
        }
        if (!found) {
            return false;
        }
        count--;
    }

    if (cmd == 't') {
        col--;
    } else if (cmd == 'T') {
        col++;
    }
    ed->col = CLAMP(col, 0, line_last_col(ed, ed->row));
    ed->preferred_col = ed->col;
    ed->last_find_cmd = cmd;
    ed->last_find_char = ch;
    return true;
}

static bool search_from(NextviEditor *ed, const char *needle, int dir, int count)
{
    if (!needle[0]) {
        return false;
    }

    int row = ed->row;
    int col = ed->col;
    while (count > 0) {
        bool found = false;
        if (dir > 0) {
            for (int r = row; r < ed->line_count; r++) {
                char *start = ed->lines[r] + (r == row ? MIN(col + 1, ed->lengths[r]) : 0);
                char *hit = strstr(start, needle);
                if (hit) {
                    row = r;
                    col = hit - ed->lines[r];
                    found = true;
                    break;
                }
            }
        } else {
            for (int r = row; r >= 0; r--) {
                int limit = r == row ? col : ed->lengths[r];
                for (int c = MAX(0, limit - 1); c >= 0; c--) {
                    if (!strncmp(&ed->lines[r][c], needle, strlen(needle))) {
                        row = r;
                        col = c;
                        found = true;
                        break;
                    }
                }
                if (found) {
                    break;
                }
            }
        }
        if (!found) {
            return false;
        }
        count--;
    }

    ed->row = row;
    ed->col = col;
    ed->preferred_col = col;
    return true;
}

static bool matching_pair(NextviEditor *ed)
{
    const char *opens = "([{";
    const char *closes = ")]}";
    char ch = ed->lines[ed->row][ed->col];
    const char *open_hit = strchr(opens, ch);
    const char *close_hit = strchr(closes, ch);
    if (!open_hit && !close_hit) {
        for (int c = ed->col; c < ed->lengths[ed->row]; c++) {
            ch = ed->lines[ed->row][c];
            open_hit = strchr(opens, ch);
            close_hit = strchr(closes, ch);
            if (open_hit || close_hit) {
                ed->col = c;
                break;
            }
        }
    }
    if (!open_hit && !close_hit) {
        return false;
    }

    int dir = open_hit ? 1 : -1;
    char left = open_hit ? ch : opens[close_hit - closes];
    char right = open_hit ? closes[open_hit - opens] : ch;
    int depth = 0;
    int row = ed->row;
    int col = ed->col;

    while (row >= 0 && row < ed->line_count) {
        col += dir;
        while (col >= 0 && col < ed->lengths[row]) {
            char c = ed->lines[row][col];
            if (dir > 0 && c == left) {
                depth++;
            } else if (dir > 0 && c == right) {
                if (!depth) {
                    ed->row = row;
                    ed->col = col;
                    ed->preferred_col = col;
                    return true;
                }
                depth--;
            } else if (dir < 0 && c == right) {
                depth++;
            } else if (dir < 0 && c == left) {
                if (!depth) {
                    ed->row = row;
                    ed->col = col;
                    ed->preferred_col = col;
                    return true;
                }
                depth--;
            }
            col += dir;
        }
        row += dir;
        if (row >= 0 && row < ed->line_count) {
            col = dir > 0 ? -1 : ed->lengths[row];
        }
    }
    return false;
}

static void yank_set(NextviEditor *ed, const char *s, int len, bool linewise)
{
    free(ed->yank);
    ed->yank = xstrndup(s, len);
    ed->yank_len = len;
    ed->yank_linewise = linewise;
}

static char *region_text(NextviEditor *ed, Cursor beg, Cursor end, bool linewise, int *out_len)
{
    if (linewise) {
        beg.col = 0;
        end.col = ed->lengths[end.row];
    }
    int cap = 64;
    int len = 0;
    char *out = xmalloc(cap);
    for (int r = beg.row; r <= end.row; r++) {
        int c1 = r == beg.row ? beg.col : 0;
        int c2 = r == end.row ? end.col : ed->lengths[r];
        if (!linewise && r == end.row) {
            c2 = MIN(c2 + 1, ed->lengths[r]);
        }
        c1 = CLAMP(c1, 0, ed->lengths[r]);
        c2 = CLAMP(c2, c1, ed->lengths[r]);
        int add = c2 - c1 + (r < end.row || linewise ? 1 : 0);
        if (len + add + 1 > cap) {
            while (len + add + 1 > cap) {
                cap *= 2;
            }
            out = xrealloc(out, cap);
        }
        memcpy(out + len, ed->lines[r] + c1, c2 - c1);
        len += c2 - c1;
        if (r < end.row || linewise) {
            out[len++] = '\n';
        }
    }
    out[len] = '\0';
    *out_len = len;
    return out;
}

static void delete_region_raw(NextviEditor *ed, Cursor beg, Cursor end, bool linewise)
{
    if (linewise) {
        int count = end.row - beg.row + 1;
        ed->row = beg.row;
        ed->col = 0;
        while (count-- > 0) {
            delete_line_raw(ed, beg.row);
        }
        ed->row = CLAMP(beg.row, 0, ed->line_count - 1);
        ed->col = first_nonblank(ed, ed->row);
        return;
    }

    if (beg.row == end.row) {
        int row = beg.row;
        int c1 = CLAMP(beg.col, 0, ed->lengths[row]);
        int c2 = CLAMP(end.col + 1, c1, ed->lengths[row]);
        memmove(&ed->lines[row][c1], &ed->lines[row][c2], ed->lengths[row] - c2 + 1);
        ed->lengths[row] -= c2 - c1;
        ed->row = row;
        ed->col = MIN(c1, line_last_col(ed, row));
        return;
    }

    int head_len = beg.col;
    int tail_col = MIN(end.col + 1, ed->lengths[end.row]);
    int tail_len = ed->lengths[end.row] - tail_col;
    ensure_text_capacity(ed, beg.row, head_len + tail_len + 1);
    memcpy(&ed->lines[beg.row][head_len], &ed->lines[end.row][tail_col], tail_len + 1);
    ed->lengths[beg.row] = head_len + tail_len;
    for (int r = end.row; r > beg.row; r--) {
        delete_line_raw(ed, r);
    }
    ed->row = beg.row;
    ed->col = MIN(beg.col, line_last_col(ed, beg.row));
}

static void apply_case_region_raw(NextviEditor *ed, Cursor beg, Cursor end, bool linewise, PendingOp op)
{
    if (linewise) {
        beg.col = 0;
        end.col = ed->lengths[end.row] ? ed->lengths[end.row] - 1 : 0;
    }
    for (int r = beg.row; r <= end.row; r++) {
        int c1 = r == beg.row ? beg.col : 0;
        int c2 = r == end.row ? end.col : ed->lengths[r] - 1;
        for (int c = MAX(0, c1); c <= c2 && c < ed->lengths[r]; c++) {
            unsigned char ch = (unsigned char)ed->lines[r][c];
            if (op == OP_CASE_SWAP) {
                ed->lines[r][c] = islower(ch) ? (char)toupper(ch) : (char)tolower(ch);
            } else if (op == OP_CASE_LOWER) {
                ed->lines[r][c] = (char)tolower(ch);
            } else if (op == OP_CASE_UPPER) {
                ed->lines[r][c] = (char)toupper(ch);
            }
        }
    }
}

static void shift_lines_raw(NextviEditor *ed, int beg, int end, int dir)
{
    for (int r = beg; r <= end; r++) {
        if (dir > 0) {
            ed->row = r;
            ed->col = 0;
            insert_char_raw(ed, ' ');
            insert_char_raw(ed, ' ');
        } else {
            int remove = 0;
            while (remove < 2 && remove < ed->lengths[r] && ed->lines[r][remove] == ' ') {
                remove++;
            }
            if (remove) {
                memmove(ed->lines[r], ed->lines[r] + remove, ed->lengths[r] - remove + 1);
                ed->lengths[r] -= remove;
            }
        }
    }
}

static void apply_operator(NextviEditor *ed, PendingOp op, Cursor beg, Cursor end, bool linewise)
{
    ordered_region(beg, end, &beg, &end);

    if (op == OP_YANK || op == OP_DELETE || op == OP_CHANGE) {
        int len = 0;
        char *text = region_text(ed, beg, end, linewise, &len);
        yank_set(ed, text, len, linewise);
        free(text);
    }

    if (op == OP_YANK) {
        ed->row = beg.row;
        ed->col = beg.col;
        editor_show_status(ed, "yanked");
        return;
    }

    begin_change(ed);
    if (op == OP_DELETE || op == OP_CHANGE) {
        delete_region_raw(ed, beg, end, linewise);
        ed->modified = true;
        if (op == OP_CHANGE) {
            set_mode(ed, NEXTVI_MODE_INSERT, "insert");
        } else {
            set_mode(ed, NEXTVI_MODE_NORMAL, "deleted");
        }
    } else if (op == OP_SHIFT_LEFT || op == OP_SHIFT_RIGHT) {
        shift_lines_raw(ed, beg.row, end.row, op == OP_SHIFT_RIGHT ? 1 : -1);
        ed->row = beg.row;
        ed->col = first_nonblank(ed, ed->row);
        ed->modified = true;
    } else {
        apply_case_region_raw(ed, beg, end, linewise, op);
        ed->row = beg.row;
        ed->col = beg.col;
        ed->modified = true;
    }
}

static void paste_yank(NextviEditor *ed, bool after)
{
    if (!ed->yank || !ed->yank_len) {
        editor_show_status(ed, "nothing to put");
        return;
    }
    begin_change(ed);
    if (ed->yank_linewise) {
        int row = ed->row + (after ? 1 : 0);
        int start = row;
        int beg = 0;
        while (beg < ed->yank_len) {
            int end = beg;
            while (end < ed->yank_len && ed->yank[end] != '\n') {
                end++;
            }
            char *line = xstrndup(ed->yank + beg, end - beg);
            insert_line_raw(ed, row++, line, end - beg, MAX(NEXTVI_INITIAL_LINE_LEN, end - beg + 1));
            beg = end + 1;
        }
        ed->row = CLAMP(start, 0, ed->line_count - 1);
        ed->col = first_nonblank(ed, ed->row);
    } else {
        if (after && ed->col < ed->lengths[ed->row]) {
            ed->col++;
        }
        for (int i = 0; i < ed->yank_len; i++) {
            if (ed->yank[i] == '\n') {
                split_line_raw(ed);
            } else {
                insert_char_raw(ed, ed->yank[i]);
            }
        }
        if (ed->col > 0) {
            ed->col--;
        }
    }
    hardwrap_reflow(ed, ed->row);
    ed->modified = true;
    editor_show_status(ed, "put");
}

static void undo(NextviEditor *ed)
{
    if (!ed->undo_len) {
        editor_show_status(ed, "undo failed");
        return;
    }
    history_push(ed->redo, &ed->redo_len, snapshot_make(ed));
    BufferSnapshot snap = ed->undo[--ed->undo_len];
    snapshot_restore(ed, &snap);
    ed->modified = true;
    clamp_cursor(ed);
    editor_show_status(ed, "undo");
}

static void redo(NextviEditor *ed)
{
    if (!ed->redo_len) {
        editor_show_status(ed, "redo failed");
        return;
    }
    history_push(ed->undo, &ed->undo_len, snapshot_make(ed));
    BufferSnapshot snap = ed->redo[--ed->redo_len];
    snapshot_restore(ed, &snap);
    ed->modified = true;
    clamp_cursor(ed);
    editor_show_status(ed, "redo");
}

static bool event_to_key(uint8_t event, EditorKey *key)
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
    key->ctrl = KBD_MODS & 2;
    key->vk = shifted ? keymap_shift[code] : keymap[code];
    key->ascii = 0;
    if (key->vk >= VKCHAROFFSET && key->vk <= VK_z) {
        int idx = key->vk - VKCHAROFFSET;
        if (unicodemap[idx] >= 0x20 && unicodemap[idx] <= 0x7e) {
            key->ascii = unicodemap[idx];
        }
    }
    if (key->ctrl && key->ascii) {
        key->ascii = NEXTVI_CTRL(tolower((unsigned char)key->ascii));
    }
    return true;
}

static bool printable_key(EditorKey key)
{
    return key.ascii >= 0x20 && key.ascii <= 0x7e && !key.ctrl;
}

static void handle_insert_key(NextviEditor *ed, EditorKey key)
{
    if (key.vk == VK_ESC) {
        set_mode(ed, NEXTVI_MODE_NORMAL, "normal");
        if (ed->col > 0) {
            ed->col--;
        }
        ed->preferred_col = ed->col;
        return;
    }

    if (key.vk == VK_LEFT) {
        ed->col = MAX(0, ed->col - 1);
    } else if (key.vk == VK_RIGHT) {
        ed->col = MIN(ed->lengths[ed->row], ed->col + 1);
    } else if (key.vk == VK_UP) {
        move_vertical(ed, -1);
    } else if (key.vk == VK_DOWN) {
        move_vertical(ed, 1);
    } else {
        if (key.vk == VK_ENTER || key.vk == VK_TAB || printable_key(key)) {
            splash_begin_edit(ed);
        }
        begin_change(ed);
        if (key.vk == VK_ENTER) {
            split_line_raw(ed);
        } else if (key.vk == VK_BACKSPACE) {
            backspace_raw(ed);
        } else if (key.vk == VK_DELETE) {
            delete_char_raw(ed);
        } else if (key.vk == VK_TAB) {
            insert_char_raw(ed, ' ');
            insert_char_raw(ed, ' ');
        } else if (printable_key(key)) {
            if (ed->mode == NEXTVI_MODE_REPLACE && ed->col < ed->lengths[ed->row]) {
                ed->lines[ed->row][ed->col++] = (char)key.ascii;
                ed->preferred_col = ed->col;
            } else {
                insert_char_raw(ed, (char)key.ascii);
            }
        } else {
            ed->undo_len--;
            snapshot_free(&ed->undo[ed->undo_len]);
            return;
        }
        ed->modified = true;
        hardwrap_reflow(ed, ed->row);
    }
    ed->preferred_col = ed->col;
}

static void begin_prompt(NextviEditor *ed, PromptKind kind)
{
    ed->prev_mode = ed->mode;
    ed->mode = kind == PROMPT_COMMAND ? NEXTVI_MODE_COMMAND : NEXTVI_MODE_SEARCH;
    ed->prompt_kind = kind;
    ed->prompt_len = 0;
    ed->prompt[0] = '\0';
    ed->status_visible = false;
}

static void execute_command(NextviEditor *ed)
{
    char *cmd = ed->prompt;
    while (*cmd == ' ') {
        cmd++;
    }
    if (!strcmp(cmd, "w") || !strcmp(cmd, "write")) {
        nextvi_storage_save(ed, NULL);
    } else if (!strcmp(cmd, "e") || !strcmp(cmd, "edit")) {
        nextvi_storage_load(ed, NULL);
    } else if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) {
        editor_show_status(ed, "quit not available");
    } else if (!strcmp(cmd, "wq") || !strcmp(cmd, "x")) {
        nextvi_storage_save(ed, NULL);
    } else if (!strcmp(cmd, "set")) {
        editor_show_status(ed, "set not available");
    } else if (cmd[0] == '%' && cmd[1] == 'd' && !cmd[2]) {
        begin_change(ed);
        while (ed->line_count > 1) {
            delete_line_raw(ed, 0);
        }
        ed->lines[0][0] = '\0';
        ed->lengths[0] = 0;
        ed->row = 0;
        ed->col = 0;
        ed->modified = true;
        editor_show_status(ed, "buffer cleared");
    } else if (!strcmp(cmd, "gw")) {
        begin_change(ed);
        if (hardwrap_reflow(ed, ed->row)) {
            ed->modified = true;
        }
        editor_show_status(ed, "wrapped line");
    } else if (!strcmp(cmd, "gq")) {
        begin_change(ed);
        hardwrap_all(ed);
        ed->modified = true;
        editor_show_status(ed, "wrapped buffer");
    } else {
        editor_show_status(ed, "ex command unavailable");
    }
}

static void handle_prompt_key(NextviEditor *ed, EditorKey key)
{
    if (key.vk == VK_ESC) {
        set_mode(ed, NEXTVI_MODE_NORMAL, "normal");
        return;
    }
    if (key.vk == VK_BACKSPACE) {
        if (ed->prompt_len > 0) {
            ed->prompt[--ed->prompt_len] = '\0';
        }
        return;
    }
    if (key.vk == VK_ENTER) {
        if (ed->prompt_kind == PROMPT_COMMAND) {
            set_mode(ed, NEXTVI_MODE_NORMAL, NULL);
            execute_command(ed);
        } else {
            snprintf(ed->search, sizeof(ed->search), "%s", ed->prompt);
            ed->search_dir = ed->prompt_kind == PROMPT_SEARCH_BACKWARD ? -1 : 1;
            set_mode(ed, NEXTVI_MODE_NORMAL, NULL);
            if (search_from(ed, ed->search, ed->search_dir, 1)) {
                editor_show_status(ed, "match");
            } else {
                editor_show_status(ed, "not found");
            }
        }
        return;
    }
    if (printable_key(key) && ed->prompt_len < NEXTVI_CMD_MAX) {
        ed->prompt[ed->prompt_len++] = (char)key.ascii;
        ed->prompt[ed->prompt_len] = '\0';
    }
}

static MotionKind motion_from_key(NextviEditor *ed, int ch, int count, Cursor *out)
{
    int old_row = ed->row;
    switch (ch) {
    case 'h':
        ed->col = MAX(0, ed->col - count);
        break;
    case 'l':
    case ' ':
        ed->col = MIN(line_last_col(ed, ed->row), ed->col + count);
        break;
    case 'j':
    case '+':
    case '\n':
        move_vertical(ed, count);
        break;
    case 'k':
    case '-':
        move_vertical(ed, -count);
        break;
    case '0':
        ed->col = 0;
        break;
    case '^':
        ed->col = first_nonblank(ed, ed->row);
        break;
    case '$':
        ed->col = ed->lengths[ed->row] ? ed->lengths[ed->row] - 1 : 0;
        break;
    case 'w':
    case 'W':
        motion_word_forward(ed, count, ch == 'W', false);
        break;
    case 'e':
    case 'E':
        motion_word_forward(ed, count, ch == 'E', true);
        break;
    case 'b':
    case 'B':
        motion_word_backward(ed, count, ch == 'B');
        break;
    case 'G':
        move_to_line(ed, count > 1 ? count - 1 : ed->line_count - 1);
        break;
    case '%':
        if (!matching_pair(ed)) {
            return MOTION_NONE;
        }
        break;
    case 'H':
        move_to_line(ed, ed->top);
        break;
    case 'M':
        move_to_line(ed, ed->top + TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS / 2);
        break;
    case 'L':
        move_to_line(ed, ed->top + TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS - 1);
        break;
    default:
        return MOTION_NONE;
    }

    ed->preferred_col = ed->col;
    *out = (Cursor){ed->row, ed->col};
    return old_row == ed->row ? MOTION_CHAR : MOTION_LINE;
}

static void open_line(NextviEditor *ed, bool below)
{
    if (ed->splash_visible) {
        (void)below;
        set_mode(ed, NEXTVI_MODE_INSERT, "insert");
        return;
    }
    begin_change(ed);
    int row = ed->row + (below ? 1 : 0);
    insert_line_raw(ed, row, line_new_empty(), 0, NEXTVI_INITIAL_LINE_LEN);
    ed->row = row;
    ed->col = 0;
    ed->preferred_col = 0;
    ed->modified = true;
    set_mode(ed, NEXTVI_MODE_INSERT, "insert");
}

static void visual_apply(NextviEditor *ed, PendingOp op)
{
    Cursor beg;
    Cursor end;
    bool linewise = ed->mode == NEXTVI_MODE_VISUAL_LINE;
    ordered_region(ed->visual_anchor, (Cursor){ed->row, ed->col}, &beg, &end);
    ed->mode = NEXTVI_MODE_NORMAL;
    apply_operator(ed, op, beg, end, linewise);
    clear_pending(ed);
}

static void handle_normal_key(NextviEditor *ed, EditorKey key)
{
    if (key.vk == VK_ESC) {
        clear_pending(ed);
        set_mode(ed, NEXTVI_MODE_NORMAL, "normal");
        return;
    }

    int ch = key.ascii;
    if (key.vk == VK_LEFT) {
        ch = 'h';
    } else if (key.vk == VK_DOWN) {
        ch = 'j';
    } else if (key.vk == VK_UP) {
        ch = 'k';
    } else if (key.vk == VK_RIGHT) {
        ch = 'l';
    } else if (key.vk == VK_PAGEDOWN) {
        ch = NEXTVI_CTRL('f');
    } else if (key.vk == VK_PAGEUP) {
        ch = NEXTVI_CTRL('b');
    }

    if (ed->awaiting_replace) {
        ed->awaiting_replace = false;
        if (printable_key(key) && ed->lengths[ed->row]) {
            begin_change(ed);
            ed->lines[ed->row][ed->col] = (char)key.ascii;
            ed->modified = true;
        }
        clear_pending(ed);
        return;
    }

    if (ed->awaiting_find) {
        ed->awaiting_find = false;
        if (printable_key(key) && !find_char_on_line(ed, ed->find_cmd, (char)key.ascii, command_count(ed))) {
            editor_show_status(ed, "not found");
        }
        clear_pending(ed);
        return;
    }

    if (ed->awaiting_mark) {
        ed->awaiting_mark = false;
        if (ch >= 'a' && ch <= 'z') {
            int idx = ch - 'a';
            if (ed->awaiting_mark_set) {
                ed->marks[idx] = (Cursor){ed->row, ed->col};
                ed->mark_set[idx] = true;
                editor_show_status(ed, "mark set");
            } else if (ed->mark_set[idx]) {
                ed->row = ed->marks[idx].row;
                ed->col = ed->awaiting_mark_line ? first_nonblank(ed, ed->row) :
                          MIN(ed->marks[idx].col, line_last_col(ed, ed->row));
                ed->preferred_col = ed->col;
            } else {
                editor_show_status(ed, "mark not set");
            }
        }
        clear_pending(ed);
        return;
    }

    if (ed->awaiting_g) {
        ed->awaiting_g = false;
        int count = command_count(ed);
        if (ch == 'g') {
            move_to_line(ed, count > 1 ? count - 1 : 0);
        } else if (ch == 'w') {
            begin_change(ed);
            if (hardwrap_reflow(ed, ed->row)) {
                ed->modified = true;
            }
            editor_show_status(ed, "wrapped line");
        } else if (ch == 'q') {
            begin_change(ed);
            hardwrap_all(ed);
            ed->modified = true;
            editor_show_status(ed, "wrapped buffer");
        } else if (ch == '~' || ch == 'u' || ch == 'U') {
            PendingOp op = ch == '~' ? OP_CASE_SWAP : ch == 'u' ? OP_CASE_LOWER : OP_CASE_UPPER;
            Cursor end;
            ed->count = count;
            if (motion_from_key(ed, 'l', count, &end) != MOTION_NONE) {
                apply_operator(ed, op, (Cursor){ed->row, MAX(0, ed->col - count + 1)}, end, false);
            }
        }
        clear_pending(ed);
        return;
    }

    if (ed->awaiting_z) {
        ed->awaiting_z = false;
        if (ch == '\n') {
            ed->top = ed->row;
        } else if (ch == '.') {
            ed->top = MAX(0, ed->row - TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS / 2);
        } else if (ch == '-') {
            ed->top = MAX(0, ed->row - TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS + 1);
        }
        clear_pending(ed);
        return;
    }

    if (ed->pending_op != OP_NONE) {
        PendingOp op = ed->pending_op;
        Cursor start = {ed->row, ed->col};
        int count = command_count(ed);
        clear_pending(ed);

        if ((op == OP_DELETE || op == OP_CHANGE || op == OP_YANK) &&
                ((op == OP_DELETE && ch == 'd') || (op == OP_CHANGE && ch == 'c') || (op == OP_YANK && ch == 'y'))) {
            Cursor end = {MIN(ed->line_count - 1, start.row + count - 1), ed->lengths[MIN(ed->line_count - 1, start.row + count - 1)]};
            apply_operator(ed, op, start, end, true);
            return;
        }
        if ((op == OP_SHIFT_LEFT && ch == '<') || (op == OP_SHIFT_RIGHT && ch == '>')) {
            Cursor end = {MIN(ed->line_count - 1, start.row + count - 1), 0};
            apply_operator(ed, op, start, end, true);
            return;
        }

        Cursor end;
        MotionKind kind = motion_from_key(ed, ch, count, &end);
        if (kind != MOTION_NONE) {
            apply_operator(ed, op, start, end, kind == MOTION_LINE);
        }
        return;
    }

    if (ed->mode == NEXTVI_MODE_VISUAL || ed->mode == NEXTVI_MODE_VISUAL_LINE) {
        int count = command_count(ed);
        Cursor ignored;
        switch (ch) {
        case 'v':
        case 'V':
        case 27:
            set_mode(ed, NEXTVI_MODE_NORMAL, "normal");
            return;
        case 'd':
        case 'x':
            visual_apply(ed, OP_DELETE);
            return;
        case 'c':
            visual_apply(ed, OP_CHANGE);
            return;
        case 'y':
            visual_apply(ed, OP_YANK);
            set_mode(ed, NEXTVI_MODE_NORMAL, "normal");
            return;
        case '~':
            visual_apply(ed, OP_CASE_SWAP);
            return;
        case 'u':
            visual_apply(ed, OP_CASE_LOWER);
            return;
        case 'U':
            visual_apply(ed, OP_CASE_UPPER);
            return;
        case '<':
            visual_apply(ed, OP_SHIFT_LEFT);
            return;
        case '>':
            visual_apply(ed, OP_SHIFT_RIGHT);
            return;
        case 'o': {
            Cursor cur = {ed->row, ed->col};
            ed->row = ed->visual_anchor.row;
            ed->col = ed->visual_anchor.col;
            ed->visual_anchor = cur;
            return;
        }
        default:
            if (motion_from_key(ed, ch, count, &ignored) != MOTION_NONE) {
                return;
            }
            break;
        }
    }

    if (ch >= '1' && ch <= '9') {
        ed->count = ed->count * 10 + (ch - '0');
        return;
    }
    if (ch == '0' && ed->count) {
        ed->count *= 10;
        return;
    }

    int count = command_count(ed);
    Cursor ignored;
    switch (ch) {
    case NEXTVI_CTRL('b'):
        move_vertical(ed, -TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS * count);
        ed->top = MAX(0, ed->top - TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS * count);
        break;
    case NEXTVI_CTRL('f'):
        move_vertical(ed, TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS * count);
        ed->top = MIN(MAX(0, ed->line_count - 1), ed->top + TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS * count);
        break;
    case NEXTVI_CTRL('u'):
        move_vertical(ed, -(TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS / 2) * count);
        break;
    case NEXTVI_CTRL('d'):
        move_vertical(ed, (TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS / 2) * count);
        break;
    case NEXTVI_CTRL('e'):
        ed->top = MIN(MAX(0, ed->line_count - 1), ed->top + count);
        break;
    case NEXTVI_CTRL('y'):
        ed->top = MAX(0, ed->top - count);
        break;
    case 'i':
        set_mode(ed, NEXTVI_MODE_INSERT, "insert");
        break;
    case 'I':
        ed->col = first_nonblank(ed, ed->row);
        set_mode(ed, NEXTVI_MODE_INSERT, "insert");
        break;
    case 'a':
        ed->col = MIN(ed->lengths[ed->row], ed->col + 1);
        set_mode(ed, NEXTVI_MODE_INSERT, "insert");
        break;
    case 'A':
        ed->col = ed->lengths[ed->row];
        set_mode(ed, NEXTVI_MODE_INSERT, "insert");
        break;
    case 'R':
        set_mode(ed, NEXTVI_MODE_REPLACE, "replace");
        break;
    case 'o':
        open_line(ed, true);
        break;
    case 'O':
        open_line(ed, false);
        break;
    case 'x':
        begin_change(ed);
        for (int i = 0; i < count; i++) {
            delete_char_raw(ed);
        }
        ed->modified = true;
        break;
    case 'X':
        begin_change(ed);
        while (count-- > 0 && ed->col > 0) {
            ed->col--;
            delete_char_raw(ed);
        }
        ed->modified = true;
        break;
    case 'D':
        apply_operator(ed, OP_DELETE, (Cursor){ed->row, ed->col}, (Cursor){ed->row, line_last_col(ed, ed->row)}, false);
        break;
    case 'C':
        apply_operator(ed, OP_CHANGE, (Cursor){ed->row, ed->col}, (Cursor){ed->row, line_last_col(ed, ed->row)}, false);
        break;
    case 'S':
        apply_operator(ed, OP_CHANGE, (Cursor){ed->row, 0}, (Cursor){ed->row, ed->lengths[ed->row]}, true);
        break;
    case 's':
        apply_operator(ed, OP_CHANGE, (Cursor){ed->row, ed->col},
                       (Cursor){ed->row, MIN(line_last_col(ed, ed->row), ed->col + count - 1)}, false);
        break;
    case 'Y':
        apply_operator(ed, OP_YANK, (Cursor){ed->row, 0}, (Cursor){MIN(ed->line_count - 1, ed->row + count - 1), 0}, true);
        break;
    case 'J':
        begin_change(ed);
        for (int i = 0; i < count && ed->row + 1 < ed->line_count; i++) {
            join_with_next_raw(ed, true);
        }
        hardwrap_reflow(ed, ed->row);
        ed->modified = true;
        break;
    case 'p':
    case 'P':
        paste_yank(ed, ch == 'p');
        break;
    case 'u':
        undo(ed);
        break;
    case NEXTVI_CTRL('r'):
        redo(ed);
        break;
    case 'r':
        ed->awaiting_replace = true;
        ed->count = count;
        editor_show_status(ed, "replace char");
        break;
    case 'd':
        ed->pending_op = OP_DELETE;
        ed->count = count;
        break;
    case 'c':
        ed->pending_op = OP_CHANGE;
        ed->count = count;
        break;
    case 'y':
        ed->pending_op = OP_YANK;
        ed->count = count;
        break;
    case '<':
        ed->pending_op = OP_SHIFT_LEFT;
        ed->count = count;
        break;
    case '>':
        ed->pending_op = OP_SHIFT_RIGHT;
        ed->count = count;
        break;
    case '~':
        begin_change(ed);
        apply_case_region_raw(ed, (Cursor){ed->row, ed->col}, (Cursor){ed->row, ed->col + count - 1}, false, OP_CASE_SWAP);
        ed->col = MIN(line_last_col(ed, ed->row), ed->col + count);
        ed->modified = true;
        break;
    case 'v':
        ed->visual_anchor = (Cursor){ed->row, ed->col};
        set_mode(ed, NEXTVI_MODE_VISUAL, "visual");
        break;
    case 'V':
        ed->visual_anchor = (Cursor){ed->row, ed->col};
        set_mode(ed, NEXTVI_MODE_VISUAL_LINE, "visual line");
        break;
    case ':':
        begin_prompt(ed, PROMPT_COMMAND);
        break;
    case '/':
        begin_prompt(ed, PROMPT_SEARCH_FORWARD);
        break;
    case '?':
        begin_prompt(ed, PROMPT_SEARCH_BACKWARD);
        break;
    case 'n':
    case 'N':
        if (!search_from(ed, ed->search, ch == 'n' ? ed->search_dir : -ed->search_dir, count)) {
            editor_show_status(ed, "not found");
        }
        break;
    case 'f':
    case 'F':
    case 't':
    case 'T':
        ed->awaiting_find = true;
        ed->find_cmd = ch;
        ed->count = count;
        break;
    case ';':
        if (!ed->last_find_char || !find_char_on_line(ed, ed->last_find_cmd, ed->last_find_char, count)) {
            editor_show_status(ed, "not found");
        }
        break;
    case ',':
        if (ed->last_find_char) {
            int cmd = ed->last_find_cmd == 'f' ? 'F' : ed->last_find_cmd == 'F' ? 'f' :
                      ed->last_find_cmd == 't' ? 'T' : 't';
            if (!find_char_on_line(ed, cmd, ed->last_find_char, count)) {
                editor_show_status(ed, "not found");
            }
        }
        break;
    case 'm':
        ed->awaiting_mark = true;
        ed->awaiting_mark_set = true;
        ed->count = count;
        editor_show_status(ed, "mark");
        break;
    case '`':
    case '\'':
        ed->awaiting_mark = true;
        ed->awaiting_mark_set = false;
        ed->awaiting_mark_line = ch == '\'';
        editor_show_status(ed, "jump mark");
        break;
    case 'g':
        ed->awaiting_g = true;
        ed->count = count;
        break;
    case 'z':
        ed->awaiting_z = true;
        ed->count = count;
        break;
    case 'G':
        move_to_line(ed, count > 1 ? count - 1 : ed->line_count - 1);
        break;
    case 'H':
    case 'M':
    case 'L':
    case 'h':
    case 'j':
    case 'k':
    case 'l':
    case ' ':
    case '0':
    case '^':
    case '$':
    case 'w':
    case 'W':
    case 'e':
    case 'E':
    case 'b':
    case 'B':
    case '%':
    case '+':
    case '-':
    case '\n':
        ed->count = count;
        motion_from_key(ed, ch, count, &ignored);
        ed->count = 0;
        break;
    case VK_ESC:
        clear_pending(ed);
        set_mode(ed, NEXTVI_MODE_NORMAL, "normal");
        break;
    default:
        break;
    }
}

void nextvi_esp_run(QueueHandle_t keyboard)
{
    NextviEditor editor;
    uint8_t event;
    EditorKey key;

    editor_init(&editor);
    editor_draw(&editor);

    while (1) {
        TickType_t wait_ticks = editor.status_visible ? pdMS_TO_TICKS(100) : portMAX_DELAY;
        if (xQueueReceive(keyboard, &event, wait_ticks) == pdTRUE) {
            if (event_to_key(event, &key)) {
                if (editor_hide_expired_status(&editor)) {
                    editor_draw(&editor);
                }
                if (editor.mode == NEXTVI_MODE_INSERT || editor.mode == NEXTVI_MODE_REPLACE) {
                    handle_insert_key(&editor, key);
                } else if (editor.mode == NEXTVI_MODE_COMMAND || editor.mode == NEXTVI_MODE_SEARCH) {
                    handle_prompt_key(&editor, key);
                } else {
                    handle_normal_key(&editor, key);
                }
                editor_draw(&editor);
            }
        } else {
            if (editor_hide_expired_status(&editor)) {
                editor_draw(&editor);
            }
        }
        vTaskDelay(1);
    }

    editor_free(&editor);
}
