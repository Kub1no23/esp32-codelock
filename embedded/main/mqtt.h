#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>
#include <stdbool.h>
#include "ili9341.h"

typedef enum
{
    STATE_IDLE,
    STATE_WAITING_FOR_CLOUD,
    STATE_UNLOCKED
} lock_state_t;

void mqtt_app_start(ili9341_t *dev);
void send_pin_to_cloud(const char *pin);
void mqtt_check_timeout(void);
lock_state_t get_lock_state(void);
void reset_lock_state(void);

#endif