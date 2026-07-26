/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "kscan_gpio.h"

#include <stdlib.h>

/*
 * Insertion sort rather than qsort(): the lists are a handful of entries and
 * sorted once at init, while pulling in libc qsort costs ~730 bytes of flash.
 * That is worth caring about on parts like the 62 KB CH32X035, and it is free
 * everywhere else. Stable, and O(n^2) is irrelevant at this size.
 */
void kscan_gpio_list_sort_by_port(struct kscan_gpio_list *list) {
    for (size_t i = 1; i < list->len; i++) {
        const struct kscan_gpio key = list->gpios[i];
        size_t j = i;

        while (j > 0 && list->gpios[j - 1].spec.port > key.spec.port) {
            list->gpios[j] = list->gpios[j - 1];
            j--;
        }

        list->gpios[j] = key;
    }
}

int kscan_gpio_pin_get(const struct kscan_gpio *gpio, struct kscan_gpio_port_state *state) {
    if (gpio->spec.port != state->port) {
        state->port = gpio->spec.port;

        const int err = gpio_port_get(state->port, &state->value);
        if (err) {
            return err;
        }
    }

    return (state->value & BIT(gpio->spec.pin)) != 0;
}
