/*
 * Low battery / charging LED indicator for ZMK (PWM version)
 *
 * USB plugged in + battery < 100%  → breathing fade (charging)
 * USB plugged in + battery = 100%  → LED off (fully charged)
 * USB unplugged  + battery <= 10%  → LED solid on (low battery)
 * USB unplugged  + battery > 10%   → LED off (normal)
 *
 * Detects USB power via nRF52840 POWER peripheral (no USB stack needed).
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>

#include <hal/nrf_power.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#define LOW_BATTERY_THRESHOLD 10
#define LED_NODE DT_ALIAS(low_battery_led)

/* Breathing parameters */
#define BREATHE_TICK_MS    20   /* 50 Hz update rate */
#define BREATHE_HALF_STEPS 75   /* ticks for one ramp direction */
/* Full cycle = 2 * 75 * 20ms = 3000ms = 3 seconds */

#if DT_NODE_EXISTS(LED_NODE)

static const struct pwm_dt_spec led = PWM_DT_SPEC_GET(LED_NODE);
static bool led_initialized = false;

static uint8_t battery_level = 0;

/* Breathing state */
static int breathe_pos = 0;
static bool breathe_rising = true;

static void breathe_timer_cb(struct k_timer *timer);
K_TIMER_DEFINE(breathe_timer, breathe_timer_cb, NULL);

static bool is_usb_power_present(void) {
    return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

static void set_led_brightness(uint32_t pulse) {
    if (!led_initialized) {
        return;
    }
    pwm_set_pulse_dt(&led, pulse);
}

static void breathe_timer_cb(struct k_timer *timer) {
    if (breathe_rising) {
        breathe_pos++;
        if (breathe_pos >= BREATHE_HALF_STEPS) {
            breathe_rising = false;
        }
    } else {
        breathe_pos--;
        if (breathe_pos <= 0) {
            breathe_rising = true;
        }
    }

    uint32_t pulse = (led.period * breathe_pos) / BREATHE_HALF_STEPS;
    set_led_brightness(pulse);
}

enum led_mode {
    LED_OFF,
    LED_SOLID,
    LED_BREATHE,
};

static void update_led_state(void) {
    enum led_mode mode;
    bool usb_power = is_usb_power_present();

    if (usb_power) {
        mode = (battery_level < 100) ? LED_BREATHE : LED_OFF;
    } else {
        mode = (battery_level <= LOW_BATTERY_THRESHOLD) ? LED_SOLID : LED_OFF;
    }

    switch (mode) {
    case LED_BREATHE:
        breathe_pos = 0;
        breathe_rising = true;
        k_timer_start(&breathe_timer, K_MSEC(BREATHE_TICK_MS), K_MSEC(BREATHE_TICK_MS));
        break;
    case LED_SOLID:
        k_timer_stop(&breathe_timer);
        set_led_brightness(led.period);
        break;
    case LED_OFF:
        k_timer_stop(&breathe_timer);
        set_led_brightness(0);
        break;
    }
}

/* DEBUG: set LED to full brightness to verify PWM works */
static int low_battery_led_init(void) {
    if (!pwm_is_ready_dt(&led)) {
        return -ENODEV;
    }

    led_initialized = true;
    set_led_brightness(led.period);

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

    battery_level = ev->state_of_charge;
    update_led_state();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(low_battery_led, battery_listener);
ZMK_SUBSCRIPTION(low_battery_led, zmk_battery_state_changed);

#endif /* DT_NODE_EXISTS(LED_NODE) */
