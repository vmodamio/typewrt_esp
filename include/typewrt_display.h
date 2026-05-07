#pragma once

#include <stdint.h>

#define TYPEWRT_DISPLAY_WIDTH 320
#define TYPEWRT_DISPLAY_HEIGHT 240
#define TYPEWRT_DISPLAY_GLYPH_HEIGHT 16
#define TYPEWRT_DISPLAY_TEXT_COLUMNS 40
#define TYPEWRT_DISPLAY_TEXT_ROWS 14
#define TYPEWRT_DISPLAY_PHYSICAL_TEXT_ROWS 15
#define TYPEWRT_DISPLAY_STATUS_ROW 14

void typewrt_display_init(void);
void typewrt_display_clear(void);
void typewrt_display_clear_buffer(void);
void typewrt_display_refresh(void);
void typewrt_display_update_text_row(uint8_t row);
void typewrt_display_set_pixel(int16_t x, int16_t y, uint16_t color);
uint8_t typewrt_display_get_pixel(uint16_t x, uint16_t y);
void typewrt_display_draw_glyph(uint8_t glyph_index, uint8_t column, uint8_t row);
void typewrt_display_draw_bitmap(int16_t x, int16_t y, uint16_t width, uint16_t height,
                                 const uint8_t *bitmap, uint16_t stride_bytes);
void typewrt_display_draw_text_line(uint8_t row, const char *text, int text_len,
                                    int cursor_column, int cursor_visible);
