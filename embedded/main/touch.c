#include "touch.h"
#include "keypad.h"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static TaskHandle_t touch_task_handle = NULL;
static const char *TAG = "TOUCH";
extern char input_buffer[7];

static uint16_t xpt2046_read_channel(spi_device_handle_t spi, uint8_t cmd)
{
    uint8_t tx_data[3] = {cmd, 0x00, 0x00};
    uint8_t rx_data[3] = {0};

    esp_err_t ret = spi_device_polling_transmit(spi, &(spi_transaction_t){
                                                         .length = 8 * 3,
                                                         .tx_buffer = tx_data,
                                                         .rx_buffer = rx_data,
                                                     });
    if (ret != ESP_OK)
    {
        return 0;
    }

    uint16_t raw_val = ((rx_data[1] << 8) | rx_data[2]) >> 3;
    return raw_val & 0x0FFF;
}

bool touch_read_raw(spi_device_handle_t spi, uint16_t *out_raw_x, uint16_t *out_raw_y)
{
    if (gpio_get_level(PIN_NUM_TOUCH_IRQ) != 0)
    {
        return false;
    }

    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    const int samples = 4;

    for (int i = 0; i < samples; i++)
    {
        sum_x += xpt2046_read_channel(spi, 0x90); // X axis
        sum_y += xpt2046_read_channel(spi, 0xD0); // Y axis
    }

    *out_raw_x = sum_x / samples;
    *out_raw_y = sum_y / samples;

    return true;
}

static inline int32_t clamp(int32_t val, int32_t min, int32_t max)
{
    if (val < min)
        return min;
    if (val > max)
        return max;

    return val;
}

static inline int32_t map_range(int32_t val, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

bool touch_get_xy(ili9341_t *dev, uint16_t *touch_x, uint16_t *touch_y)
{
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;

    if (!touch_read_raw(dev->touch_spi, &raw_x, &raw_y))
    {
        return false;
    }
    ESP_LOGI(TAG, "RAW ADC -> X: %u, Y: %u", raw_x, raw_y);

    int32_t clamped_raw_x = clamp(raw_x, TOUCH_RAW_MIN_X, TOUCH_RAW_MAX_X);
    int32_t clamped_raw_y = clamp(raw_y, TOUCH_RAW_MIN_Y, TOUCH_RAW_MAX_Y);

    int32_t x = map_range(clamped_raw_y, TOUCH_RAW_MIN_Y, TOUCH_RAW_MAX_Y, 0, dev->width - 1);
    int32_t y = map_range(clamped_raw_x, TOUCH_RAW_MIN_X, TOUCH_RAW_MAX_X, dev->height - 1, 0);

    *touch_x = (uint16_t)clamp(x, 0, dev->width - 1);
    *touch_y = (uint16_t)clamp(y, 0, dev->height - 1);

    return true;
}

static void IRAM_ATTR touch_isr_handler(void *arg)
{
    BaseType_t high_task_wakeup = pdFALSE;

    vTaskNotifyGiveFromISR(touch_task_handle, &high_task_wakeup);
    if (high_task_wakeup)
    {
        portYIELD_FROM_ISR();
    }
}

static void touch_task(void *pvParameters)
{
    ili9341_t *dev = (ili9341_t *)pvParameters;
    while (1)
    {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(300)) > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(20));

            uint16_t touch_x = 0, touch_y = 0;

            if (!touch_get_xy(dev, &touch_x, &touch_y))
            {
                ESP_LOGE(TAG, "Failed to get touch coordinates");
            }
            else
            {
                ESP_LOGI(TAG, "Touch coordinates -> X: %u, Y: %u", touch_x, touch_y);
                keypad_control_logic(dev, touch_x, touch_y);

                while (gpio_get_level(PIN_NUM_TOUCH_IRQ) == 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            while (ulTaskNotifyTake(pdTRUE, 0) > 0)
            {
            }
        }
        else
        {
            keypad_update_animation(dev);
        }
    }
}

void touch_init(ili9341_t *dev)
{
    xTaskCreatePinnedToCore(touch_task, "touch_task", 4096, (void *)dev, 5, &touch_task_handle, 1);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_NUM_TOUCH_IRQ, touch_isr_handler, NULL));
};
