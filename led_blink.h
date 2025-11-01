// led_blink.h

#ifndef LED_BLINK_H
#define LED_BLINK_H

#include "pico/stdlib.h"

#define LED_PIN 25

void start_led_blinking(uint interval_ms);

#endif