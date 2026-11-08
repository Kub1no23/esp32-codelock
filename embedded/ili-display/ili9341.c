#include "ili9341.h"

#define GPIO_LEVEL_LOW ((uint32_t)0)
#define GPIO_LEVEL_HIGH ((uint32_t)1)

void ili9341_reset(gpio_num_t pin_rst)
{
    if (pin_rst < 0)
        return;

    ESP_ERROR_CHECK(gpio_set_level(pin_rst, GPIO_LEVEL_HIGH));
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_ERROR_CHECK(gpio_set_level(pin_rst, GPIO_LEVEL_LOW));
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_ERROR_CHECK(gpio_set_level(pin_rst, GPIO_LEVEL_HIGH));

    vTaskDelay(pdMS_TO_TICKS(120));
}

void ili9341_write_cmd(ili9341_t *dev, uint8_t cmd)
{
    ESP_ERROR_CHECK(gpio_set_level(dev->pin_dc, GPIO_LEVEL_LOW)); // 0 cmd, 1 data

    ESP_ERROR_CHECK(
        spi_device_polling_transmit(dev->spi, &(spi_transaction_t){
                                                  .length = 8,
                                                  .tx_buffer = &cmd,
                                              }));
}

void ili9341_write_data(ili9341_t *dev, const uint8_t *data, size_t len)
{
    if (len == 0)
        return;

    ESP_ERROR_CHECK(gpio_set_level(dev->pin_dc, GPIO_LEVEL_HIGH));

    ESP_ERROR_CHECK(
        spi_device_polling_transmit(dev->spi, &(spi_transaction_t){
                                                  .length = len * 8,
                                                  .tx_buffer = data,
                                              }));
}

void ili9341_setup(ili9341_t *dev)
{
    ili9341_reset(dev->pin_rst);

    size_t count = sizeof(init_conf) / sizeof(ili9341_init_t);

    for (size_t i = 0; i < count; i++)
    {
        ili9341_write_cmd(dev, init_conf[i].cmd);

        if (init_conf[i].data_len > 0)
        {
            ili9341_write_data(dev, init_conf[i].data, init_conf[i].data_len);
        }

        if (init_conf[i].delay_ms > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(init_conf[i].delay_ms));
        }
    }
}

void ili9341_set_rotation(ili9341_t *dev, ili9341_rotation_t rotation)
{
    uint8_t val = 0;
    val = 1U << ILI9341_MADCTL_BGR;

    switch (rotation)
    {
    case ILI9341_ROTATION_PORTRAIT: // 240x320
        val |= 1U << ILI9341_MADCTL_MX;
        break;
    case ILI9341_ROTATION_PORTRAIT_FLIP:
        break;
    case ILI9341_ROTATION_PORTRAIT_180:
        val |= 1U << ILI9341_MADCTL_MY;
        break;
    case ILI9341_ROTATION_PORTRAIT_FLIP_180:
        val |= 1U << ILI9341_MADCTL_MX | 1U << ILI9341_MADCTL_MY;
        break;
    case ILI9341_ROTATION_LANDSCAPE: // 320x240
        val |= 1U << ILI9341_MADCTL_MX | 1U << ILI9341_MADCTL_MV;
        break;
    case ILI9341_ROTATION_LANDSCAPE_FLIP:
        val |= 1U << ILI9341_MADCTL_MV;
        break;
    case ILI9341_ROTATION_LANDSCAPE_180:
        val |= 1U << ILI9341_MADCTL_MY | 1U << ILI9341_MADCTL_MV;
        break;
    case ILI9341_ROTATION_LANDSCAPE_FLIP_180:
        val |= 1U << ILI9341_MADCTL_MX | 1U << ILI9341_MADCTL_MY | 1U << ILI9341_MADCTL_MV;
        break;
    }

    if (val & 1U << ILI9341_MADCTL_MV)
    {
        dev->width = 320;
        dev->height = 240;
    }
    else
    {
        dev->width = 240;
        dev->height = 320;
    }

    ili9341_write_cmd(dev, ILI9341_CMD_MADCTL);
    ili9341_write_data(dev, &val, 1);
}

void ili9341_set_addr_window(ili9341_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    // col addr set - width
    data[0] = (x0 >> 8) & 0xFF;
    data[1] = x0 & 0xFF;
    data[2] = (x1 >> 8) & 0xFF;
    data[3] = x1 & 0xFF;
    ili9341_write_cmd(dev, ILI9341_CMD_CASET);
    ili9341_write_data(dev, data, 4);

    // row addr set - height
    data[0] = (y0 >> 8) & 0xFF;
    data[1] = y0 & 0xFF;
    data[2] = (y1 >> 8) & 0xFF;
    data[3] = y1 & 0xFF;
    ili9341_write_cmd(dev, ILI9341_CMD_PASET);
    ili9341_write_data(dev, data, 4);

    ili9341_write_cmd(dev, ILI9341_CMD_RAMWR);
}

void ili9341_fill_area(ili9341_t *dev, uint16_t x0, uint16_t y0, uint16_t width, uint16_t height, uint16_t color)
{
    if (width == 0 || height == 0)
        return;

    if (x0 >= dev->width || y0 >= dev->height)
        return;

    uint16_t x1 = x0 + width - 1;
    uint16_t y1 = y0 + height - 1;
    if (x1 >= dev->width)
        x1 = dev->width - 1;
    if (y1 >= dev->height)
        y1 = dev->height - 1;

    ili9341_set_addr_window(dev, x0, y0, x1, y1);

    uint8_t color_bytes[2] = {(color >> 8) & 0xFF, color & 0xFF};

    size_t total_pixels = (x1 - x0 + 1) * (y1 - y0 + 1);
    for (size_t i = 0; i < total_pixels; i++)
    {
        ili9341_write_data(dev, color_bytes, 2);
    }
}

void ili9341_draw_pixel(ili9341_t *dev, uint16_t x, uint16_t y, uint16_t color)
{
    ili9341_fill_area(dev, x, y, 1, 1, color);
}

void ili9341_fill_screen(ili9341_t *dev, uint16_t color)
{
    ili9341_fill_area(dev, 0, 0, dev->width, dev->height, color);
}