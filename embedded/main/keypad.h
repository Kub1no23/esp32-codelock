#ifndef KEYPAD_H
#define KEYPAD_H

#include "ili9341.h"
#include "esp_err.h"
#include "esp_log.h"

#define KEYPAD_START_X 11
#define KEYPAD_START_Y 67
#define KEYPAD_WIDTH 218
#define KEYPAD_HEIGHT 243
#define BTN_W 72
#define BTN_H 60
#define BORDER_SIZE 1

#define FIELD_START_X 11
#define FIELD_START_Y 10
#define FIELD_WIDTH 218
#define FIELD_HEIGHT 48
#define FIELD_BG_COLOR 0x0000
#define TEXT_COLOR 0xFFFF
#define CHAR_SPACING 5

void keypad_draw_all(ili9341_t *dev);
void keypad_draw_button(ili9341_t *dev, uint16_t x, uint16_t y, char label, uint16_t bg_color, uint16_t fg_color);
void ili9341_draw_char(ili9341_t *dev, uint16_t x, uint16_t y, char c, uint16_t fg_color, uint8_t scale);
void keypad_update_animation(ili9341_t *dev);
void keypad_control_logic(ili9341_t *dev, uint16_t touch_x, uint16_t touch_y);
void input_field_draw(ili9341_t *dev, uint16_t start_idx);
void input_field_draw_status(ili9341_t *dev, char status);

#endif