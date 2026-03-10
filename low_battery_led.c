/*
 * Low battery LED indicator for ZMK
 * Turns LED on solid when battery <= 10%, off when above.
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#define LOW_BATTERY_THRESHOLD 10
#define LED_NODE DT_ALIAS(low_battery_led)

#if !DT_NODE_EXISTS(LED_NODE)
#error "Device tree alias 'low-battery-led' not found"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
static bool led_initialized = false;

static int low_battery_led_init(void) {
    if (!gpio_is_ready_dt(&led)) {
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    led_initialized = true;
    return 0;
}

SYS_INIT(low_battery_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static int battery_listener(const zmk_event_t *eh) {
    if (!led_initialized) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint8_t level = ev->state_of_charge;

    if (level <= LOW_BATTERY_THRESHOLD) {
        gpio_pin_set_dt(&led, 1);
    } else {
        gpio_pin_set_dt(&led, 0);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(low_battery_led, battery_listener);
ZMK_SUBSCRIPTION(low_battery_led, zmk_battery_state_changed);
