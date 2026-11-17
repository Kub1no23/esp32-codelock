#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "mqtt.h"
#include "esp_crt_bundle.h"
#include "keypad.h"

static const char *TAG = "AZURE_MQTT";

#define TOPIC_PUBLISH_TELEMETRY "devices/" CONFIG_AZURE_DEVICE_ID "/messages/events/"
#define TOPIC_SUBSCRIBE_C2D "devices/" CONFIG_AZURE_DEVICE_ID "/messages/devicebound/#"
#define TOPIC_SUBSCRIBE_METHODS "$iothub/methods/POST/#"

static volatile lock_state_t current_state = STATE_IDLE;
static uint64_t time_pin_sent_us = 0;
static esp_mqtt_client_handle_t mqtt_client = NULL;

static esp_timer_handle_t reset_timer = NULL;

lock_state_t get_lock_state(void)
{
    return current_state;
}

void reset_lock_state(void)
{
    current_state = STATE_IDLE;
}

static void reset_callback(void *arg)
{
    ili9341_t *dev = (ili9341_t *)arg;

    reset_lock_state();

    if (xSemaphoreTake(dev->spi_lock, portMAX_DELAY) == pdTRUE)
    {
        input_field_draw(dev, 0, NULL);
        xSemaphoreGive(dev->spi_lock);
    }

    ESP_LOGI(TAG, "State reset");
}

static void execute_direct_method(ili9341_t *dev, esp_mqtt_client_handle_t client, const char *method_name, const char *rid)
{
    int status_code = 404;
    const char *response_payload = "{}";
    bool reboot = false;

    if (strcmp(method_name, "get_status") == 0)
    {
        ESP_LOGI(TAG, "Executing 'get_status'...");
        response_payload = "{\"device_state\": \"online\"}";
        status_code = 200;
    }
    else if (strcmp(method_name, "open_lock") == 0)
    {
        ESP_LOGI(TAG, "Executing 'open_lock'...");

        current_state = STATE_UNLOCKED;
        if (xSemaphoreTake(dev->spi_lock, portMAX_DELAY) == pdTRUE)
        {
            input_field_draw_status(dev, 'V');
            xSemaphoreGive(dev->spi_lock);
        }

        esp_timer_start_once(reset_timer, 3000000);

        response_payload = "{\"success\":true}";
        status_code = 200;
    }
    else if (strcmp(method_name, "reboot_device") == 0)
    {
        ESP_LOGW(TAG, "Executing 'reboot_device'...");
        reboot = true;

        response_payload = "{\"success\":true}";
        status_code = 200;
    }
    else
    {
        ESP_LOGE(TAG, "Unknown method: %s", method_name);
        response_payload = "{\"success\":false,\"error\":\"method not found\"}";
    }

    char response_topic[128];
    snprintf(response_topic, sizeof(response_topic), "$iothub/methods/res/%d/?$rid=%s", status_code, rid);

    esp_mqtt_client_publish(client, response_topic, response_payload, 0, 1, 0);
    ESP_LOGI(TAG, "Sent method response: %s", response_payload);

    if (reboot)
    {
        ESP_LOGW(TAG, "Rebooting in 1 second...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
}

static void process_cloud_message(ili9341_t *dev, const char *data, size_t data_len)
{
    char *json_str = malloc(data_len + 1);
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (root == NULL)
    {
        ESP_LOGE(TAG, "Err with parsing JSON from cloud: %s", cJSON_GetErrorPtr());
        return;
    }

    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (cJSON_IsString(action) && (action->valuestring != NULL))
    {
        if (strcmp(action->valuestring, "unlock") == 0)
        {
            ESP_LOGI(TAG, "Cloud confirmed PIN");
            current_state = STATE_UNLOCKED;

            if (xSemaphoreTake(dev->spi_lock, portMAX_DELAY) == pdTRUE)
            {
                input_field_draw_status(dev, 'V');
                xSemaphoreGive(dev->spi_lock);
            }

            // reset
            esp_timer_start_once(reset_timer, 3000000); // 3s
        }
        else if (strcmp(action->valuestring, "deny") == 0)
        {
            ESP_LOGW(TAG, "Cloud rejected PIN");
            current_state = STATE_REJECTED;

            if (xSemaphoreTake(dev->spi_lock, portMAX_DELAY) == pdTRUE)
            {
                input_field_draw_status(dev, 'X');
                xSemaphoreGive(dev->spi_lock);
            }

            // reset
            esp_timer_start_once(reset_timer, 3000000);
        }
    }

    cJSON_Delete(root);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    ili9341_t *dev = (ili9341_t *)handler_args;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected to Azure IoT Hub");
        esp_mqtt_client_subscribe(mqtt_client, TOPIC_SUBSCRIBE_C2D, 1);
        esp_mqtt_client_subscribe(mqtt_client, TOPIC_SUBSCRIBE_METHODS, 1);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Received message from cloud. Topic len: %d, Data len: %d", event->topic_len, event->data_len);

        if (strncmp(event->topic, "devices/" CONFIG_AZURE_DEVICE_ID "/messages/devicebound/", 44) == 0)
        {
            if (current_state == STATE_WAITING_FOR_CLOUD)
            {
                process_cloud_message(dev, event->data, event->data_len);
            }
            else
            {
                ESP_LOGW(TAG, "Ignoring message, lock is not waiting for confirmation");
            }
        }
        else if (strncmp(event->topic, "$iothub/methods/POST/", 21) == 0)
        {
            ESP_LOGI(TAG, "Received direct method call from cloud. Topic len: %d, Data len: %d", event->topic_len, event->data_len);

            char safe_topic[128];
            if (event->topic_len >= sizeof(safe_topic))
            {
                ESP_LOGE(TAG, "Topic is too long to parse safely");
                return;
            }
            memcpy(safe_topic, event->topic, event->topic_len);
            safe_topic[event->topic_len] = '\0';

            char method_name[32] = {0};
            char rid[16] = {0};

            if (sscanf(safe_topic + 21, "%31[^/]/?$rid=%15s", method_name, rid) == 2)
            {
                ESP_LOGI(TAG, "Method: %s, RID: %s", method_name, rid);

                execute_direct_method(dev, event->client, method_name, rid);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to parse method name or RID from topic");
            }
        }

        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT Disconnected");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT Error");
        break;

    default:
        break;
    }
}

void mqtt_app_start(ili9341_t *dev)
{
    if (reset_timer == NULL)
    {
        const esp_timer_create_args_t timer_args = {
            .callback = &reset_callback,
            .arg = dev,
            .name = "reset_timer"};
        esp_err_t err = esp_timer_create(&timer_args, &reset_timer);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to create reset timer");
        }
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .credentials.client_id = CONFIG_AZURE_DEVICE_ID,
        .broker.address.uri = CONFIG_AZURE_MQTT_BROKER_URI,
        .credentials.username = CONFIG_AZURE_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_AZURE_MQTT_PASSWORD,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach};

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, dev);
    esp_err_t err = esp_mqtt_client_start(mqtt_client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start MQTT client");
    }
}

void send_pin_to_cloud(const char *pin)
{
    if (mqtt_client == NULL)
        return;

    char payload[128];
    snprintf(payload, sizeof(payload), "{\"devId\":\"%s\",\"code\":\"%s\"}", CONFIG_AZURE_DEVICE_ID, pin);

    ESP_LOGI(TAG, "Sending telemetry: %s", payload);

    esp_mqtt_client_publish(mqtt_client, TOPIC_PUBLISH_TELEMETRY, payload, 0, 1, 0);

    current_state = STATE_WAITING_FOR_CLOUD;
    time_pin_sent_us = esp_timer_get_time();
}

void mqtt_check_timeout(void)
{
    if (current_state == STATE_WAITING_FOR_CLOUD)
    {
        uint64_t current_time_us = esp_timer_get_time();

        if ((current_time_us - time_pin_sent_us) > 10000000) // 10 sec
        {
            ESP_LOGE(TAG, "TIMEOUT: Cloud did not respond within 10 seconds");

            // Zde můžeš fyzicky pípnout bzučákem nebo vypsat chybu na displej
            current_state = STATE_IDLE;
        }
    }
}