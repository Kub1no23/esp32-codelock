#ifndef ILI9341_H
#define ILI9341_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"       // Pro ovládání pinů (DC, RST, CS...)
#include "driver/spi_master.h" // Pro hardwarové SPI transakce
#include "esp_err.h"           // Typ esp_err_t a chybové návratové kódy
#include "esp_log.h"           // Logování (ESP_LOGI, ESP_LOGE)
#include "freertos/FreeRTOS.h" // Časování a delaye
#include "freertos/task.h"

#define ILI9341_CMD_SWRESET 0x01
#define ILI9341_CMD_SLPOUT 0x11
#define ILI9341_CMD_DISPOFF 0x28
#define ILI9341_CMD_DISPON 0x29
#define ILI9341_CMD_CASET 0x2A
#define ILI9341_CMD_PASET 0x2B
#define ILI9341_CMD_RAMWR 0x2C
#define ILI9341_CMD_COLMOD 0x3A
#define ILI9341_CMD_FRMCTR1 0xB1
#define ILI9341_CMD_DFUNCTR 0xB6
#define ILI9341_CMD_PWCTR1 0xC0
#define ILI9341_CMD_PWCTR2 0xC1
#define ILI9341_CMD_VMCTR1 0xC5
#define ILI9341_CMD_VMCTR2 0xC7
#define ILI9341_CMD_GMCTRP1 0xE0
#define ILI9341_CMD_GMCTRN1 0xE1

#define ILI9341_CMD_MADCTL 0x36
#define ILI9341_MADCTL_MY 7U
#define ILI9341_MADCTL_MX 6U
#define ILI9341_MADCTL_MV 5U
#define ILI9341_MADCTL_ML 4U
#define ILI9341_MADCTL_BGR 3U
#define ILI9341_MADCTL_MH 2U

typedef enum
{
    ILI9341_ROTATION_PORTRAIT,
    ILI9341_ROTATION_PORTRAIT_FLIP,
    ILI9341_ROTATION_PORTRAIT_180,
    ILI9341_ROTATION_PORTRAIT_FLIP_180,
    ILI9341_ROTATION_LANDSCAPE,
    ILI9341_ROTATION_LANDSCAPE_FLIP,
    ILI9341_ROTATION_LANDSCAPE_180,
    ILI9341_ROTATION_LANDSCAPE_FLIP_180,
} ili9341_rotation_t;

typedef struct
{
    gpio_num_t pin_dc;
    gpio_num_t pin_rst;
    gpio_num_t pin_bckl;
    spi_device_handle_t spi;
    spi_device_handle_t touch_spi;
    short width;
    short height;
    ili9341_rotation_t rotation;
} ili9341_t;

typedef struct
{
    uint8_t cmd;
    const uint8_t *data;
    uint8_t data_len;
    uint16_t delay_ms;
} ili9341_init_t;

static const ili9341_init_t init_conf[] = {
    {ILI9341_CMD_SWRESET, NULL, 0, 150},
    {ILI9341_CMD_DISPOFF, NULL, 0, 0},

    {ILI9341_CMD_PWCTR1, (const uint8_t[]){0x23}, 1, 0},
    {ILI9341_CMD_PWCTR2, (const uint8_t[]){0x10}, 1, 0},
    {ILI9341_CMD_VMCTR1, (const uint8_t[]){0x3E, 0x28}, 2, 0},
    {ILI9341_CMD_VMCTR2, (const uint8_t[]){0x86}, 1, 0},

    {ILI9341_CMD_FRMCTR1, (const uint8_t[]){0x00, 0x18}, 2, 0},

    {ILI9341_CMD_COLMOD, (const uint8_t[]){0x55}, 1, 0},

    {ILI9341_CMD_DFUNCTR, (const uint8_t[]){0x08, 0x82, 0x27}, 3, 0},

    {ILI9341_CMD_GMCTRP1, (const uint8_t[]){0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15, 0},
    {ILI9341_CMD_GMCTRN1, (const uint8_t[]){0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15, 0},

    {ILI9341_CMD_SLPOUT, NULL, 0, 120},
    {ILI9341_CMD_DISPON, NULL, 0, 20}};

void ili9341_reset(gpio_num_t pin_rst);
void ili9341_write_cmd(ili9341_t *dev, uint8_t cmd);
void ili9341_write_data(ili9341_t *dev, const uint8_t *data, size_t len);
void ili9341_setup(ili9341_t *dev);
void ili9341_set_rotation(ili9341_t *dev, ili9341_rotation_t rotation);
void ili9341_set_addr_window(ili9341_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void ili9341_fill_area(ili9341_t *dev, uint16_t x0, uint16_t y0, uint16_t width, uint16_t height, uint16_t color);
void ili9341_fill_screen(ili9341_t *dev, uint16_t color);
void ili9341_draw_pixel(ili9341_t *dev, uint16_t x, uint16_t y, uint16_t color);

#endif