#ifndef TOUCH_H
#define TOUCH_H
#include "ili9341.h"

#define PIN_NUM_TOUCH_IRQ GPIO_NUM_18

#define TOUCH_RAW_MIN_X 250
#define TOUCH_RAW_MAX_X 3850
#define TOUCH_RAW_MIN_Y 200
#define TOUCH_RAW_MAX_Y 3800

void touch_init(ili9341_t *dev);

#endif