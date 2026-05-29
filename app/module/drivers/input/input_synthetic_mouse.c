/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Synthetic pointing device for SCI pointing tests. When enabled it reports a
 * diagonal relative movement (+step on both X and Y) every poll-period-ms,
 * reversing once the accumulated position reaches +/-amplitude -- a steady
 * triangle-wave sweep. It also drives mouse buttons from board buttons:
 *   - toggle-gpios     : start/stop the movement stream (so the link can be
 *                        observed idle vs active)
 *   - left-click-gpios : reports INPUT_BTN_0 (left click) on press/release
 *   - right-click-gpios: reports INPUT_BTN_1 (right click) on press/release
 * Everything flows through the Zephyr input subsystem, so on a split peripheral
 * it is forwarded to the central via zmk,input-split with no keymap involved.
 */

#define DT_DRV_COMPAT zmk_input_synthetic_mouse

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SYN_TOGGLE_CODE 0xFFFFU

struct syn_button {
    struct gpio_dt_spec gpio;
    struct gpio_callback cb;
    const struct device *dev;
    int64_t last;
    uint16_t code; /* INPUT_BTN_* to report, or SYN_TOGGLE_CODE for the toggle */
};

struct syn_mouse_config {
    uint16_t poll_period_ms;
    int16_t step;
    int16_t amplitude;
    bool start_enabled;
};

struct syn_mouse_data {
    const struct device *dev;
    struct k_work_delayable work;
    struct syn_button toggle;
    struct syn_button left;
    struct syn_button right;
    int16_t pos;
    int16_t dir;
    volatile bool enabled;
};

static void syn_mouse_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct syn_mouse_data *data = CONTAINER_OF(dwork, struct syn_mouse_data, work);
    const struct device *dev = data->dev;
    const struct syn_mouse_config *cfg = dev->config;

    if (!data->enabled) {
        return;
    }

    int16_t d = cfg->step * data->dir;

    /* Diagonal: X then Y, sync on the last so they form one report. */
    input_report_rel(dev, INPUT_REL_X, d, false, K_NO_WAIT);
    input_report_rel(dev, INPUT_REL_Y, d, true, K_NO_WAIT);

    data->pos += d;
    if ((data->pos >= cfg->amplitude) || (data->pos <= -cfg->amplitude)) {
        data->dir = -data->dir;
    }

    k_work_schedule(&data->work, K_MSEC(cfg->poll_period_ms));
}

static void syn_button_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    struct syn_button *btn = CONTAINER_OF(cb, struct syn_button, cb);
    int64_t now = k_uptime_get();

    /* Debounce: ignore edges within 20 ms of the last accepted one. */
    if ((now - btn->last) < 20) {
        return;
    }
    btn->last = now;

    if (btn->code == SYN_TOGGLE_CODE) {
        struct syn_mouse_data *data = btn->dev->data;

        data->enabled = !data->enabled;
        if (data->enabled) {
            k_work_schedule(&data->work, K_NO_WAIT);
        }
    } else {
        int pressed = gpio_pin_get_dt(&btn->gpio);

        if (pressed >= 0) {
            input_report_key(btn->dev, btn->code, pressed ? 1 : 0, true, K_NO_WAIT);
        }
    }
}

static int syn_button_setup(struct syn_button *btn, const struct device *dev, uint16_t code,
                            gpio_flags_t int_flags) {
    if (btn->gpio.port == NULL) {
        return 0;
    }
    if (!gpio_is_ready_dt(&btn->gpio)) {
        LOG_ERR("synthetic-mouse button GPIO not ready");
        return -ENODEV;
    }

    btn->dev = dev;
    btn->code = code;
    btn->last = 0;

    gpio_pin_configure_dt(&btn->gpio, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&btn->gpio, int_flags);
    gpio_init_callback(&btn->cb, syn_button_isr, BIT(btn->gpio.pin));
    gpio_add_callback(btn->gpio.port, &btn->cb);
    return 0;
}

static int syn_mouse_init(const struct device *dev) {
    struct syn_mouse_data *data = dev->data;
    const struct syn_mouse_config *cfg = dev->config;
    int ret;

    data->dev = dev;
    data->pos = 0;
    data->dir = 1;
    data->enabled = cfg->start_enabled;

    k_work_init_delayable(&data->work, syn_mouse_work_cb);

    ret = syn_button_setup(&data->toggle, dev, SYN_TOGGLE_CODE, GPIO_INT_EDGE_TO_ACTIVE);
    ret = ret ? ret : syn_button_setup(&data->left, dev, INPUT_BTN_0, GPIO_INT_EDGE_BOTH);
    ret = ret ? ret : syn_button_setup(&data->right, dev, INPUT_BTN_1, GPIO_INT_EDGE_BOTH);
    if (ret) {
        return ret;
    }

    if (data->enabled) {
        k_work_schedule(&data->work, K_MSEC(cfg->poll_period_ms));
    }

    LOG_INF("synthetic-mouse: period=%ums step=%d amp=%d enabled=%d", cfg->poll_period_ms,
            cfg->step, cfg->amplitude, data->enabled);
    return 0;
}

#define SYN_MOUSE_INST(n)                                                                          \
    static struct syn_mouse_data syn_mouse_data_##n = {                                            \
        .toggle = {.gpio = GPIO_DT_SPEC_INST_GET_OR(n, toggle_gpios, {0})},                        \
        .left = {.gpio = GPIO_DT_SPEC_INST_GET_OR(n, left_click_gpios, {0})},                      \
        .right = {.gpio = GPIO_DT_SPEC_INST_GET_OR(n, right_click_gpios, {0})},                    \
    };                                                                                             \
    static const struct syn_mouse_config syn_mouse_cfg_##n = {                                     \
        .poll_period_ms = DT_INST_PROP(n, poll_period_ms),                                         \
        .step = DT_INST_PROP(n, step),                                                             \
        .amplitude = DT_INST_PROP(n, amplitude),                                                   \
        .start_enabled = DT_INST_PROP(n, start_enabled),                                           \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, syn_mouse_init, NULL, &syn_mouse_data_##n, &syn_mouse_cfg_##n,        \
                          POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(SYN_MOUSE_INST)
