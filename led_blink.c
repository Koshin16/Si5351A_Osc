#include "led_blink.h"
#include "hardware/timer.h"

static struct repeating_timer led_timer;

static bool toggle_led(struct repeating_timer *t) {
    static bool state = false;
    gpio_put(LED_PIN, state);
    state = !state;
    return true; // 繰り返す
}

void start_led_blinking(uint interval_ms) {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    add_repeating_timer_ms(interval_ms, toggle_led, NULL, &led_timer);
}