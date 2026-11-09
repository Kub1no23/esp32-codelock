#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "esp_log.h"
#include "ili9341.h"
#include "keypad.h"
#include "touch.h"
#include "mqtt.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized.");

    // async
    esp_err_t wifi_status = wifi_init_sta();

    if (wifi_status == ESP_OK)
    {
        ESP_LOGI(TAG, "Wi-Fi initialized successfully.");
    }
    else
    {
        ESP_LOGE(TAG, "Wi-Fi initialization failed.");
        return;
    }

    ESP_LOGI(TAG, "System is running online...");

    // display
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_NUM_13,
        .miso_io_num = GPIO_NUM_19,
        .sclk_io_num = GPIO_NUM_14,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 320 * 240 * 2 + 8, // Dostatek místa i pro celoobrazovkový buffer
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40 * 1000 * 1000, // 40 MHz for ILI9341
        .mode = 0,
        .spics_io_num = GPIO_NUM_15,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    spi_device_interface_config_t touch_devcfg = {
        .clock_speed_hz = 2 * 1000 * 1000, // 2 MHz for XPT2046
        .mode = 0,
        .spics_io_num = GPIO_NUM_5,
        .queue_size = 3,
    };
    spi_device_handle_t spi;
    spi_device_handle_t touch_spi;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &touch_devcfg, &touch_spi));

    ili9341_t display_handle = {
        .pin_dc = GPIO_NUM_2,
        .pin_rst = GPIO_NUM_4,
        .spi = spi,
        .touch_spi = touch_spi,
        .rotation = ILI9341_ROTATION_PORTRAIT};

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << display_handle.pin_dc) | (1ULL << display_handle.pin_rst),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_config_t irq_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_TOUCH_IRQ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&irq_conf));

    touch_init(&display_handle);

    ili9341_setup(&display_handle);
    ili9341_set_rotation(&display_handle, display_handle.rotation);
    ili9341_fill_screen(&display_handle, 0x0000);
    keypad_draw_all(&display_handle);

    mqtt_app_start(&display_handle);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        mqtt_check_timeout();
    }
}