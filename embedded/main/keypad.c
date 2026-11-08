#include <stdint.h>
#include "keypad.h"
#include "ili9341.h"
#include "font_8x16.h"
#include "font_8x16.h"
#include "mqtt.h"

static const char *TAG = "KEYPAD";
static const char keypad_chars[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'X', '0', 'V'}};
static const char *one_time = "111111";
static uint8_t blink_state = 1;
static char input_buffer[7] = {0};

void keypad_draw_all(ili9341_t *dev)
{
    ili9341_fill_area(dev, KEYPAD_START_X, KEYPAD_START_Y, KEYPAD_WIDTH, KEYPAD_HEIGHT, 0xFFFF);

    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 3; col++)
        {

            uint16_t x = KEYPAD_START_X + col * (BTN_W + BORDER_SIZE);
            uint16_t y = KEYPAD_START_Y + row * (BTN_H + BORDER_SIZE);
            char c = keypad_chars[row][col];

            uint16_t bg_color = 0x0000;
            if (c == 'X')
            {
                bg_color = 0xF800; // red
            }
            else if (c == 'V')
            {
                bg_color = 0x07E0; // green
            }

            keypad_draw_button(dev, x, y, c, bg_color, 0xFFFF);
        }
    }
}

void keypad_draw_button(ili9341_t *dev, uint16_t x, uint16_t y, char label, uint16_t bg_color, uint16_t fg_color)
{
    ili9341_fill_area(dev, x, y, BTN_W, BTN_H, bg_color);

    uint16_t char_w = 8 * 3; // 1:2 ratio
    uint16_t char_h = 16 * 3;

    uint16_t pad_x = (BTN_W - char_w) / 2;
    uint16_t pad_y = (BTN_H - char_h) / 2;

    ili9341_draw_char(dev, x + pad_x, y + pad_y, label, fg_color, 3);
}

void ili9341_draw_char(ili9341_t *dev, uint16_t x, uint16_t y, char c, uint16_t fg_color, uint8_t scale)
{
    if (c < 32 || c > 126)
    {
        c = ' ';
    }

    const uint8_t *glyph = font_get_glyph(c);

    for (uint8_t row = 0; row < 16; row++)
    {
        uint8_t row_bits = glyph[row];

        for (uint8_t col = 0; col < 8; col++)
        {
            if (row_bits & (0x80 >> col))
            {
                ili9341_fill_area(dev,
                                  x + col * scale,
                                  y + row * scale,
                                  scale,
                                  scale,
                                  fg_color);
            }
        }
    }
}

char keypad_get_char(uint16_t touch_x, uint16_t touch_y)
{
    if (touch_x < KEYPAD_START_X || touch_y < KEYPAD_START_Y)
    {
        return ' ';
    }

    uint16_t rel_x = touch_x - KEYPAD_START_X;
    uint16_t rel_y = touch_y - KEYPAD_START_Y;

    uint16_t step_x = BTN_W + BORDER_SIZE;
    uint16_t step_y = BTN_H + BORDER_SIZE;

    // border check
    uint16_t col_offset = rel_x % step_x;
    uint16_t row_offset = rel_y % step_y;
    if (col_offset > BTN_W || row_offset > BTN_H)
    {
        return ' ';
    }

    int col = rel_x / step_x;
    int row = rel_y / step_y;

    if (col >= 3 || row >= 4)
    {
        return ' ';
    }

    return keypad_chars[row][col];
}

void input_field_draw(ili9341_t *dev, uint16_t start_idx)
{

    int len = strlen(input_buffer);

    uint16_t char_width = 8 * 2;
    uint16_t step_x = char_width + CHAR_SPACING;
    uint16_t start_x = FIELD_START_X + (start_idx * step_x);
    uint16_t start_y = FIELD_START_Y + 8;
    uint16_t clear_width = FIELD_WIDTH - (start_x - FIELD_START_X);

    if (clear_width > 0)
    {
        ili9341_fill_area(dev, start_x, FIELD_START_Y, clear_width, FIELD_HEIGHT, FIELD_BG_COLOR);
    }

    if (start_idx >= len)
    {
        return;
    }

    uint16_t cursor_x = start_x;
    uint16_t cursor_y = start_y;

    for (uint16_t i = start_idx; i < len; i++)
    {
        char c_to_draw;

        if (i < len - 1 || (i == len - 1 && blink_state))
        {
            c_to_draw = '*';
        }
        else
        {
            c_to_draw = input_buffer[i];
        }

        ili9341_draw_char(dev, cursor_x, cursor_y, c_to_draw, TEXT_COLOR, 2);

        cursor_x += char_width + CHAR_SPACING;
    }
}

void input_field_draw_status(ili9341_t *dev, char status)
{
    uint16_t icon_scale = 3;
    uint16_t char_width = 8 * icon_scale;   // 24
    uint16_t char_height = 16 * icon_scale; // 48

    uint16_t icon_x = (FIELD_START_X + FIELD_WIDTH) - char_width;
    uint16_t icon_y = FIELD_START_Y;

    ili9341_fill_area(dev, icon_x, icon_y, char_width, char_height, FIELD_BG_COLOR);

    if (status == 'v' || status == 'V')
    {
        ili9341_draw_char(dev, icon_x, icon_y, 'V', 0x07E0, icon_scale);
    }
    else if (status == 'x' || status == 'X')
    {
        ili9341_draw_char(dev, icon_x, icon_y, 'X', 0xF800, icon_scale);
    }
}

void keypad_update_animation(ili9341_t *dev)
{
    int len = strlen(input_buffer);

    if (len > 0 && len < 6)
    {
        blink_state = !blink_state;
        input_field_draw(dev, len - 1);
    }
}

void keypad_control_logic(ili9341_t *dev, uint16_t touch_x, uint16_t touch_y)
{
    char pressed_char = keypad_get_char(touch_x, touch_y);
    ESP_LOGI(TAG, "Char pressed: %c", pressed_char);

    if (pressed_char != ' ')
    {
        int len = strlen(input_buffer);

        if (pressed_char == 'X')
        {
            if (len >= 1)
            {
                input_buffer[len - 1] = '\0';
                input_field_draw(dev, len >= 2 ? len - 2 : len - 1);
            }
        }
        else if (pressed_char == 'V')
        {
            if (len == 6)
            {
                char password[7];
                strcpy(password, input_buffer);

                input_field_draw(dev, 0);

                if (strcmp(password, one_time) == 0)
                {
                    input_field_draw_status(dev, 'v');

                    // reset
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    input_buffer[0] = '\0';
                    input_field_draw(dev, 0);
                }
                else
                {
                    // wifi req
                    input_buffer[0] = '*';
                    input_buffer[1] = '\0';
                    send_pin_to_cloud(password);
                    // input_field_draw_status(dev, 'x');
                }
            }
        }
        else
        {
            if (len < 6)
            {
                input_buffer[len] = pressed_char;
                input_buffer[len + 1] = '\0';
                input_field_draw(dev, len > 0 ? len - 1 : 0);
            }
        }
    }
}