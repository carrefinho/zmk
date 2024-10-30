/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_split_peripheral_status_changed {
    uint8_t slot;
    bool connected;
};

ZMK_EVENT_DECLARE(zmk_split_peripheral_status_changed);
