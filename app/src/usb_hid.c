/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/usb/class/hid.h>

#include <zmk/usb.h>
#include <zmk/usb_hid.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/event_manager.h>

#if IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
#include <zmk/pointing/resolution_multipliers.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/hid_indicators.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Cross-file internal setters living in usb.c. */
void zmk_usb_state_set_powered(bool powered);
void zmk_usb_state_set_configured(bool configured);
void zmk_usb_state_set_suspended(bool suspended);
void zmk_usb_state_set_hid_iface_ready(bool ready);
bool zmk_usb_state_is_suspended(void);

/* ------------------------------------------------------------------ */
/* USBD descriptors / device / configuration                          */
/* ------------------------------------------------------------------ */

USBD_DEVICE_DEFINE(zmk_usbd,
                   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
                   CONFIG_USB_DEVICE_VID, CONFIG_USB_DEVICE_PID);

USBD_DESC_LANG_DEFINE(zmk_lang);
USBD_DESC_MANUFACTURER_DEFINE(zmk_mfr, CONFIG_USB_DEVICE_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(zmk_product, CONFIG_USB_DEVICE_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(zmk_sn)));

USBD_DESC_CONFIG_DEFINE(zmk_fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(zmk_hs_cfg_desc, "HS Configuration");

static const uint8_t zmk_usbd_attributes = USB_SCD_REMOTE_WAKEUP;

USBD_CONFIGURATION_DEFINE(zmk_fs_config, zmk_usbd_attributes, 250, &zmk_fs_cfg_desc);
USBD_CONFIGURATION_DEFINE(zmk_hs_config, zmk_usbd_attributes, 250, &zmk_hs_cfg_desc);

static const struct device *hid_dev;

/* Sem gates the next submit until the previous one is acknowledged. */
static K_SEM_DEFINE(hid_sem, 1, 1);

/* ------------------------------------------------------------------ */
/* HID class callbacks                                                */
/* ------------------------------------------------------------------ */

#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
static uint8_t hid_protocol = HID_PROTOCOL_REPORT;

static void hid_set_protocol(const struct device *dev, uint8_t protocol) {
    LOG_DBG("HID protocol -> %s", protocol == HID_PROTOCOL_BOOT ? "boot" : "report");
    hid_protocol = protocol;
}

void zmk_usb_hid_set_protocol(uint8_t protocol) { hid_protocol = protocol; }
#endif

static uint8_t *get_keyboard_report(size_t *len) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol != HID_PROTOCOL_REPORT) {
        zmk_hid_boot_report_t *boot_report = zmk_hid_get_boot_report();
        *len = sizeof(*boot_report);
        return (uint8_t *)boot_report;
    }
#endif
    struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
    *len = sizeof(*report);
    return (uint8_t *)report;
}

static void hid_iface_ready_cb(const struct device *dev, bool ready) {
    LOG_DBG("HID iface %s", ready ? "ready" : "not ready");
    zmk_usb_state_set_hid_iface_ready(ready);
}

static void hid_input_report_done(const struct device *dev, const uint8_t *report) {
    k_sem_give(&hid_sem);
}

static int hid_get_report(const struct device *dev, uint8_t type, uint8_t id, uint16_t len,
                          uint8_t *buf) {
    switch (type) {
    case HID_REPORT_TYPE_FEATURE:
        switch (id) {
#if IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
        case ZMK_HID_REPORT_ID_MOUSE: {
            struct zmk_hid_mouse_resolution_feature_report res = {};
            struct zmk_endpoint_instance endpoint = {.transport = ZMK_TRANSPORT_USB};
            struct zmk_pointing_resolution_multipliers mult =
                zmk_pointing_resolution_multipliers_get_profile(endpoint);
            res.body.wheel_res = mult.wheel;
            res.body.hwheel_res = mult.hor_wheel;
            size_t sz = MIN(sizeof(res), len);
            memcpy(buf, &res, sz);
            return sz;
        }
#endif
        default:
            return -ENOTSUP;
        }

    case HID_REPORT_TYPE_INPUT:
        switch (id) {
        case ZMK_HID_REPORT_ID_KEYBOARD: {
            size_t size;
            uint8_t *src = get_keyboard_report(&size);
            size_t sz = MIN(size, len);
            memcpy(buf, src, sz);
            return sz;
        }
        case ZMK_HID_REPORT_ID_CONSUMER: {
            struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
            size_t sz = MIN(sizeof(*report), len);
            memcpy(buf, report, sz);
            return sz;
        }
        default:
            LOG_ERR("Invalid GET_REPORT id %u", id);
            return -EINVAL;
        }

    default:
        LOG_ERR("Unsupported GET_REPORT type %u", type);
        return -ENOTSUP;
    }
}

static int hid_set_report(const struct device *dev, uint8_t type, uint8_t id, uint16_t len,
                          const uint8_t *buf) {
    switch (type) {
    case HID_REPORT_TYPE_FEATURE:
        switch (id) {
#if IS_ENABLED(CONFIG_ZMK_POINTING_SMOOTH_SCROLLING)
        case ZMK_HID_REPORT_ID_MOUSE:
            if (len != sizeof(struct zmk_hid_mouse_resolution_feature_report)) {
                return -EINVAL;
            }
            {
                struct zmk_hid_mouse_resolution_feature_report *report =
                    (struct zmk_hid_mouse_resolution_feature_report *)buf;
                struct zmk_endpoint_instance endpoint = {.transport = ZMK_TRANSPORT_USB};
                zmk_pointing_resolution_multipliers_process_report(&report->body, endpoint);
            }
            return 0;
#endif
        default:
            return -ENOTSUP;
        }

    case HID_REPORT_TYPE_OUTPUT:
        switch (id) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
        case ZMK_HID_REPORT_ID_LEDS:
            if (len != sizeof(struct zmk_hid_led_report)) {
                LOG_ERR("LED set report malformed: len=%u", len);
                return -EINVAL;
            }
            {
                struct zmk_hid_led_report *report = (struct zmk_hid_led_report *)buf;
                struct zmk_endpoint_instance endpoint = {.transport = ZMK_TRANSPORT_USB};
                zmk_hid_indicators_process_report(&report->body, endpoint);
            }
            return 0;
#endif
        default:
            LOG_ERR("Invalid SET_REPORT id %u", id);
            return -EINVAL;
        }

    default:
        LOG_ERR("Unsupported SET_REPORT type %u", type);
        return -ENOTSUP;
    }
}

static void hid_output_report(const struct device *dev, uint16_t len, const uint8_t *buf) {
    /* For HID with report IDs, the first byte of the OUT report is the
     * report ID. Route via set_report. */
    if (len < 1) {
        return;
    }
    hid_set_report(dev, HID_REPORT_TYPE_OUTPUT, buf[0], len - 1, buf + 1);
}

static const struct hid_device_ops zmk_hid_ops = {
    .iface_ready = hid_iface_ready_cb,
    .get_report = hid_get_report,
    .set_report = hid_set_report,
    .output_report = hid_output_report,
    .input_report_done = hid_input_report_done,
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    .set_protocol = hid_set_protocol,
#endif
};

/* ------------------------------------------------------------------ */
/* Send path                                                          */
/* ------------------------------------------------------------------ */

static int zmk_usb_hid_send_report(const uint8_t *report, size_t len) {
    if (!zmk_usb_is_hid_ready()) {
        return -ENODEV;
    }
    if (zmk_usb_state_is_suspended()) {
        return usbd_wakeup_request(&zmk_usbd);
    }
    if (k_sem_take(&hid_sem, K_MSEC(30)) != 0) {
        return -EAGAIN;
    }
    int err = hid_device_submit_report(hid_dev, len, report);
    if (err) {
        k_sem_give(&hid_sem);
    }
    return err;
}

int zmk_usb_hid_send_keyboard_report(void) {
    size_t len;
    uint8_t *report = get_keyboard_report(&len);
    return zmk_usb_hid_send_report(report, len);
}

int zmk_usb_hid_send_consumer_report(void) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol == HID_PROTOCOL_BOOT) {
        return -ENOTSUP;
    }
#endif
    struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
    return zmk_usb_hid_send_report((uint8_t *)report, sizeof(*report));
}

#if IS_ENABLED(CONFIG_ZMK_POINTING)
int zmk_usb_hid_send_mouse_report(void) {
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
    if (hid_protocol == HID_PROTOCOL_BOOT) {
        return -ENOTSUP;
    }
#endif
    struct zmk_hid_mouse_report *report = zmk_hid_get_mouse_report();
    return zmk_usb_hid_send_report((uint8_t *)report, sizeof(*report));
}
#endif

/* ------------------------------------------------------------------ */
/* USBD message callback                                              */
/* ------------------------------------------------------------------ */

static void zmk_usbd_msg_cb(struct usbd_context *ctx, const struct usbd_msg *msg) {
    LOG_DBG("USBD: %s", usbd_msg_type_string(msg->type));

    switch (msg->type) {
    case USBD_MSG_VBUS_READY:
        zmk_usb_state_set_powered(true);
        if (usbd_can_detect_vbus(ctx)) {
            if (usbd_enable(ctx)) {
                LOG_ERR("Failed to enable USBD on VBUS ready");
            }
        }
        break;
    case USBD_MSG_VBUS_REMOVED:
        zmk_usb_state_set_powered(false);
        if (usbd_can_detect_vbus(ctx)) {
            if (usbd_disable(ctx)) {
                LOG_ERR("Failed to disable USBD on VBUS removed");
            }
        }
        break;
    case USBD_MSG_RESET:
#if IS_ENABLED(CONFIG_ZMK_USB_BOOT)
        zmk_usb_hid_set_protocol(HID_PROTOCOL_REPORT);
#endif
        zmk_usb_state_set_configured(false);
        break;
    case USBD_MSG_SUSPEND:
        zmk_usb_state_set_suspended(true);
        break;
    case USBD_MSG_RESUME:
        zmk_usb_state_set_suspended(false);
        break;
    case USBD_MSG_CONFIGURATION:
        zmk_usb_state_set_configured(msg->status > 0);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Init                                                               */
/* ------------------------------------------------------------------ */

static int zmk_usb_hid_init(void) {
    hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
    if (!device_is_ready(hid_dev)) {
        LOG_ERR("HID device not ready");
        return -ENODEV;
    }

    int err =
        hid_device_register(hid_dev, zmk_hid_report_desc, sizeof(zmk_hid_report_desc), &zmk_hid_ops);
    if (err) {
        LOG_ERR("hid_device_register: %d", err);
        return err;
    }

    err = usbd_add_descriptor(&zmk_usbd, &zmk_lang);
    err = err ?: usbd_add_descriptor(&zmk_usbd, &zmk_mfr);
    err = err ?: usbd_add_descriptor(&zmk_usbd, &zmk_product);
    IF_ENABLED(CONFIG_HWINFO, (err = err ?: usbd_add_descriptor(&zmk_usbd, &zmk_sn);))
    if (err) {
        LOG_ERR("descriptor add: %d", err);
        return err;
    }

    if (USBD_SUPPORTS_HIGH_SPEED && usbd_caps_speed(&zmk_usbd) == USBD_SPEED_HS) {
        err = usbd_add_configuration(&zmk_usbd, USBD_SPEED_HS, &zmk_hs_config);
        err = err ?: usbd_register_all_classes(&zmk_usbd, USBD_SPEED_HS, 1, NULL);
        if (err) {
            LOG_ERR("HS config: %d", err);
            return err;
        }
        usbd_device_set_code_triple(&zmk_usbd, USBD_SPEED_HS, 0, 0, 0);
    }

    err = usbd_add_configuration(&zmk_usbd, USBD_SPEED_FS, &zmk_fs_config);
    err = err ?: usbd_register_all_classes(&zmk_usbd, USBD_SPEED_FS, 1, NULL);
    if (err) {
        LOG_ERR("FS config: %d", err);
        return err;
    }
    usbd_device_set_code_triple(&zmk_usbd, USBD_SPEED_FS, 0, 0, 0);

    err = usbd_msg_register_cb(&zmk_usbd, zmk_usbd_msg_cb);
    if (err) {
        LOG_ERR("msg cb: %d", err);
        return err;
    }

    /* If the controller can detect VBUS, defer usbd_enable until the
     * USBD_MSG_VBUS_READY message. Otherwise bring it up immediately. */
    if (!usbd_can_detect_vbus(&zmk_usbd)) {
        err = usbd_enable(&zmk_usbd);
        if (err) {
            LOG_ERR("usbd_enable: %d", err);
            return err;
        }
    }

    return 0;
}

SYS_INIT(zmk_usb_hid_init, APPLICATION, CONFIG_ZMK_USB_HID_INIT_PRIORITY);
