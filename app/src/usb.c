/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/usb.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * State maintained here in `usb.c` and updated by the USBD-next message
 * callback (registered in `usb_hid.c`). External modules consume the
 * `zmk_usb_*` helpers and never see Zephyr's USBD types directly, so we
 * keep transport-stack details out of `zmk/usb.h`.
 */
static bool is_powered;
static bool is_configured;
static bool is_suspended;
static bool hid_iface_ready;

static void raise_usb_status_changed_event(struct k_work *_work) {
    raise_zmk_usb_conn_state_changed(
        (struct zmk_usb_conn_state_changed){.conn_state = zmk_usb_get_conn_state()});
}

K_WORK_DEFINE(usb_status_notifier_work, raise_usb_status_changed_event);

void zmk_usb_state_set_powered(bool powered) {
    is_powered = powered;
    if (!powered) {
        is_configured = false;
        is_suspended = false;
    }
    k_work_submit(&usb_status_notifier_work);
}

void zmk_usb_state_set_configured(bool configured) {
    is_configured = configured;
    if (configured) {
        is_powered = true;
        is_suspended = false;
    }
    k_work_submit(&usb_status_notifier_work);
}

void zmk_usb_state_set_suspended(bool suspended) {
    is_suspended = suspended;
    k_work_submit(&usb_status_notifier_work);
}

void zmk_usb_state_set_hid_iface_ready(bool ready) {
    hid_iface_ready = ready;
    k_work_submit(&usb_status_notifier_work);
}

bool zmk_usb_state_is_suspended(void) { return is_suspended; }

enum zmk_usb_conn_state zmk_usb_get_conn_state(void) {
    if (is_configured && !is_suspended) {
        return ZMK_USB_CONN_HID;
    }
    if (is_powered) {
        return ZMK_USB_CONN_POWERED;
    }
    return ZMK_USB_CONN_NONE;
}

bool zmk_usb_is_hid_ready(void) {
    return zmk_usb_get_conn_state() == ZMK_USB_CONN_HID && hid_iface_ready;
}
