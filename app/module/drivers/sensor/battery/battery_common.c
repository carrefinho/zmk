/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <math.h>
#include <zephyr/drivers/sensor.h>

#include "battery_common.h"

int battery_channel_get(const struct battery_value *value, enum sensor_channel chan,
                        struct sensor_value *val_out) {
    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        val_out->val1 = value->millivolts / 1000;
        val_out->val2 = (value->millivolts % 1000) * 1000U;
        break;

    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        val_out->val1 = value->state_of_charge;
        val_out->val2 = 0;
        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}

uint8_t lithium_ion_mv_to_pct(int16_t bat_mv) {
    // Simple linear approximation of a battery based off adafruit's discharge graph:
    // https://learn.adafruit.com/li-ion-and-lipoly-batteries/voltages

    if (bat_mv >= 4200) {
        return 100;
    } else if (bat_mv <= 3450) {
        return 0;
    }

    return bat_mv * 2 / 15 - 459;
}

uint8_t lithium_primary_mv_to_pct(int16_t bat_mv) {

    if (bat_mv >= 2900) {
        return 100;
    } else if (bat_mv > 2800 && bat_mv <= 2500) {
        return 75;
    } else if (bat_mv > 2700 && bat_mv <= 2800) {
        return 50;
    } else if (bat_mv > 2600 && bat_mv <= 2700) {
        return 25;
    } else if (bat_mv <= 2500) {
        return 0;
    }

    return 99;
    // return (int)(2580 * pow(bat_mv, 0.0247));
}