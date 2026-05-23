/*
 * Copyright (c) 2025-2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * LE Connection Subrating for the split central<->peripheral link.
 *
 * The central drives a three-tier subrate ladder (ACTIVE -> IDLE -> DORMANT)
 * off ZMK activity state, requesting a higher subrate factor as the keyboard
 * goes quiet so its connection events thin out and the central drops toward
 * peripheral-latency-class power. The peripheral half independently requests
 * the ACTIVE tier on local activity so a keystroke originating on the
 * peripheral isn't held back waiting for the central to react.
 *
 * Requires the BT_CTLR_SUBRATING controller implementation (Core 5.3, Vol 6
 * Part B 5.1.19/5.1.20). Enabled by CONFIG_ZMK_SPLIT_BLE_SUBRATING.
 */

#include <errno.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk_subrating, CONFIG_ZMK_LOG_LEVEL);

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#define SUBRATE_TIMEOUT          CONFIG_ZMK_SPLIT_BLE_SUBRATE_TIMEOUT
#define SUBRATE_DORMANT_DELAY_MS CONFIG_ZMK_SPLIT_BLE_SUBRATE_DORMANT_DELAY

/* ACTIVE tier */
#define SUBRATE_ACTIVE_MIN         CONFIG_ZMK_SPLIT_BLE_SUBRATE_ACTIVE_MIN
#define SUBRATE_ACTIVE_MAX         CONFIG_ZMK_SPLIT_BLE_SUBRATE_ACTIVE_MAX
#define SUBRATE_ACTIVE_CN          CONFIG_ZMK_SPLIT_BLE_SUBRATE_ACTIVE_CN
#define SUBRATE_ACTIVE_MAX_LATENCY CONFIG_ZMK_SPLIT_BLE_SUBRATE_ACTIVE_MAX_LATENCY

BUILD_ASSERT(SUBRATE_ACTIVE_MAX >= SUBRATE_ACTIVE_MIN, "ACTIVE_MAX must be >= ACTIVE_MIN");
BUILD_ASSERT(SUBRATE_ACTIVE_MAX * (SUBRATE_ACTIVE_MAX_LATENCY + 1) <= 500,
             "ACTIVE_MAX * (ACTIVE_MAX_LATENCY + 1) must be <= 500");
BUILD_ASSERT(SUBRATE_ACTIVE_CN < SUBRATE_ACTIVE_MAX, "ACTIVE_CN must be < ACTIVE_MAX");
BUILD_ASSERT(SUBRATE_TIMEOUT * 2 > 3 * SUBRATE_ACTIVE_MAX * (SUBRATE_ACTIVE_MAX_LATENCY + 1),
             "TIMEOUT too low for active tier");

static const struct bt_conn_le_subrate_param active_params = {
    .subrate_min = SUBRATE_ACTIVE_MIN,
    .subrate_max = SUBRATE_ACTIVE_MAX,
    .max_latency = SUBRATE_ACTIVE_MAX_LATENCY,
    .continuation_number = SUBRATE_ACTIVE_CN,
    .supervision_timeout = SUBRATE_TIMEOUT,
};

/* IDLE tier */
#define SUBRATE_IDLE_MIN         CONFIG_ZMK_SPLIT_BLE_SUBRATE_IDLE_MIN
#define SUBRATE_IDLE_MAX         CONFIG_ZMK_SPLIT_BLE_SUBRATE_IDLE_MAX
#define SUBRATE_IDLE_CN          CONFIG_ZMK_SPLIT_BLE_SUBRATE_IDLE_CN
#define SUBRATE_IDLE_MAX_LATENCY CONFIG_ZMK_SPLIT_BLE_SUBRATE_IDLE_MAX_LATENCY

BUILD_ASSERT(SUBRATE_IDLE_MAX >= SUBRATE_IDLE_MIN, "IDLE_MAX must be >= IDLE_MIN");
BUILD_ASSERT(SUBRATE_IDLE_MAX * (SUBRATE_IDLE_MAX_LATENCY + 1) <= 500,
             "IDLE_MAX * (IDLE_MAX_LATENCY + 1) must be <= 500");
BUILD_ASSERT(SUBRATE_IDLE_CN < SUBRATE_IDLE_MAX, "IDLE_CN must be < IDLE_MAX");
BUILD_ASSERT(SUBRATE_TIMEOUT * 2 > 3 * SUBRATE_IDLE_MAX * (SUBRATE_IDLE_MAX_LATENCY + 1),
             "TIMEOUT too low for idle tier");

static const struct bt_conn_le_subrate_param idle_params = {
    .subrate_min = SUBRATE_IDLE_MIN,
    .subrate_max = SUBRATE_IDLE_MAX,
    .max_latency = SUBRATE_IDLE_MAX_LATENCY,
    .continuation_number = SUBRATE_IDLE_CN,
    .supervision_timeout = SUBRATE_TIMEOUT,
};

/* DORMANT tier */
#define SUBRATE_DORMANT_MIN         CONFIG_ZMK_SPLIT_BLE_SUBRATE_DORMANT_MIN
#define SUBRATE_DORMANT_MAX         CONFIG_ZMK_SPLIT_BLE_SUBRATE_DORMANT_MAX
#define SUBRATE_DORMANT_CN          CONFIG_ZMK_SPLIT_BLE_SUBRATE_DORMANT_CN
#define SUBRATE_DORMANT_MAX_LATENCY CONFIG_ZMK_SPLIT_BLE_SUBRATE_DORMANT_MAX_LATENCY

BUILD_ASSERT(SUBRATE_DORMANT_MAX >= SUBRATE_DORMANT_MIN, "DORMANT_MAX must be >= DORMANT_MIN");
BUILD_ASSERT(SUBRATE_DORMANT_MAX * (SUBRATE_DORMANT_MAX_LATENCY + 1) <= 500,
             "DORMANT_MAX * (DORMANT_MAX_LATENCY + 1) must be <= 500");
BUILD_ASSERT(SUBRATE_DORMANT_CN < SUBRATE_DORMANT_MAX, "DORMANT_CN must be < DORMANT_MAX");
BUILD_ASSERT(SUBRATE_TIMEOUT * 2 > 3 * SUBRATE_DORMANT_MAX * (SUBRATE_DORMANT_MAX_LATENCY + 1),
             "TIMEOUT too low for dormant tier");

static const struct bt_conn_le_subrate_param dormant_params = {
    .subrate_min = SUBRATE_DORMANT_MIN,
    .subrate_max = SUBRATE_DORMANT_MAX,
    .max_latency = SUBRATE_DORMANT_MAX_LATENCY,
    .continuation_number = SUBRATE_DORMANT_CN,
    .supervision_timeout = SUBRATE_TIMEOUT,
};

static void dormant_timer_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(dormant_work, dormant_timer_handler);

enum subrate_tier { TIER_ACTIVE, TIER_IDLE, TIER_DORMANT };
/* ZMK boots in the ACTIVE activity state, so seed the tier to ACTIVE; otherwise
 * the first ACTIVE->IDLE transition is a no-op and the idle tier never applies
 * until the keyboard has been active at least once. */
static enum subrate_tier current_tier = TIER_ACTIVE;

static void apply_subrate_to_conn(struct bt_conn *conn, void *data) {
    const struct bt_conn_le_subrate_param *params = data;
    struct bt_conn_info info;

    bt_conn_get_info(conn, &info);

    if (info.role == BT_CONN_ROLE_CENTRAL && info.state == BT_CONN_STATE_CONNECTED) {
        int err = bt_conn_le_subrate_request(conn, params);
        if (err && err != -EALREADY) {
            LOG_WRN("Failed to request subrate: %d", err);
        }
    }
}

static void set_tier(enum subrate_tier tier) {
    if (tier == current_tier) {
        return;
    }

    current_tier = tier;

    const struct bt_conn_le_subrate_param *params;
    const char *tier_name;

    switch (tier) {
    case TIER_ACTIVE:
        params = &active_params;
        tier_name = "ACTIVE";
        break;
    case TIER_IDLE:
        params = &idle_params;
        tier_name = "IDLE";
        break;
    case TIER_DORMANT:
        params = &dormant_params;
        tier_name = "DORMANT";
        break;
    default:
        return;
    }

    LOG_INF("Subrating tier: %s (factor=%d-%d, latency=%d, cn=%d)", tier_name, params->subrate_min,
            params->subrate_max, params->max_latency, params->continuation_number);

    bt_conn_le_subrate_set_defaults(params);
    bt_conn_foreach(BT_CONN_TYPE_LE, apply_subrate_to_conn, (void *)params);
}

static void dormant_timer_handler(struct k_work *work) { set_tier(TIER_DORMANT); }

static void subrate_active(void) {
    k_work_cancel_delayable(&dormant_work);
    set_tier(TIER_ACTIVE);
}

static void subrate_idle(void) {
    k_work_cancel_delayable(&dormant_work);
    set_tier(TIER_IDLE);
    k_work_schedule(&dormant_work, K_MSEC(SUBRATE_DORMANT_DELAY_MS));
}

static int subrating_activity_listener(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }

    switch (ev->state) {
    case ZMK_ACTIVITY_ACTIVE:
        subrate_active();
        break;
    case ZMK_ACTIVITY_IDLE:
    case ZMK_ACTIVITY_SLEEP:
        subrate_idle();
        break;
    default:
        LOG_WRN("Unhandled activity state: %d", ev->state);
        return -EINVAL;
    }
    return 0;
}

ZMK_LISTENER(zmk_subrating, subrating_activity_listener);
ZMK_SUBSCRIPTION(zmk_subrating, zmk_activity_state_changed);

static int zmk_subrating_init(void) {
    int err = bt_conn_le_subrate_set_defaults(&idle_params);
    if (err) {
        LOG_ERR("Failed to set subrating defaults: %d", err);
        return err;
    }

    LOG_INF("Subrating: active=%d-%d/%d, idle=%d-%d/%d, dormant=%d-%d/%d (delay=%ds)",
            SUBRATE_ACTIVE_MIN, SUBRATE_ACTIVE_MAX, SUBRATE_ACTIVE_MAX_LATENCY, SUBRATE_IDLE_MIN,
            SUBRATE_IDLE_MAX, SUBRATE_IDLE_MAX_LATENCY, SUBRATE_DORMANT_MIN, SUBRATE_DORMANT_MAX,
            SUBRATE_DORMANT_MAX_LATENCY, SUBRATE_DORMANT_DELAY_MS / 1000);

    return 0;
}

SYS_INIT(zmk_subrating_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#else /* !CONFIG_ZMK_SPLIT_ROLE_CENTRAL : peripheral half */

/*
 * Peripheral half: request the ACTIVE tier when local activity is detected so a
 * keystroke originating here is delivered promptly rather than waiting for the
 * central to drive a tier change. The central remains the authority for idle /
 * dormant transitions.
 */

static const struct bt_conn_le_subrate_param peripheral_active_params = {
    .subrate_min = 1,
    .subrate_max = 2,
    .max_latency = 0,
    .continuation_number = 1,
    .supervision_timeout = 400, /* 4 seconds */
};

static bool peripheral_is_active = false;

static void apply_subrate_to_peripheral_conn(struct bt_conn *conn, void *data) {
    const struct bt_conn_le_subrate_param *params = data;
    struct bt_conn_info info;

    bt_conn_get_info(conn, &info);

    if (info.role == BT_CONN_ROLE_PERIPHERAL && info.state == BT_CONN_STATE_CONNECTED) {
        int err = bt_conn_le_subrate_request(conn, params);
        if (err && err != -EALREADY) {
            LOG_WRN("Peripheral failed to request subrate: %d", err);
        }
    }
}

static int peripheral_subrating_activity_listener(const zmk_event_t *eh) {
    struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }

    if (ev->state == ZMK_ACTIVITY_ACTIVE) {
        if (!peripheral_is_active) {
            LOG_INF("Peripheral requesting ACTIVE subrate");
            bt_conn_foreach(BT_CONN_TYPE_LE, apply_subrate_to_peripheral_conn,
                            (void *)&peripheral_active_params);
            peripheral_is_active = true;
        }
    } else {
        peripheral_is_active = false;
    }

    return 0;
}

ZMK_LISTENER(zmk_subrating_peripheral, peripheral_subrating_activity_listener);
ZMK_SUBSCRIPTION(zmk_subrating_peripheral, zmk_activity_state_changed);

#endif /* CONFIG_ZMK_SPLIT_ROLE_CENTRAL */

/* Subrate-change logging, both roles. */

static void subrate_changed_cb(struct bt_conn *conn,
                               const struct bt_conn_le_subrate_changed *params) {
    struct bt_conn_info info;
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_conn_get_info(conn, &info);
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));

    const char *role = info.role == BT_CONN_ROLE_CENTRAL ? "central" : "peripheral";

    if (params->status == BT_HCI_ERR_SUCCESS) {
        LOG_INF("Subrating [%s %s]: factor=%d, cn=%d", role, addr_str, params->factor,
                params->continuation_number);
    } else {
        LOG_WRN("Subrating failed [%s %s]: 0x%02x", role, addr_str, params->status);
    }
}

static void le_param_updated_cb(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                                uint16_t timeout) {
    struct bt_conn_info info;
    char addr_str[BT_ADDR_LE_STR_LEN];

    bt_conn_get_info(conn, &info);
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));

    const char *role = info.role == BT_CONN_ROLE_CENTRAL ? "central" : "peripheral";

    /* interval is in 1.25ms units; timeout in 10ms units. A param update on the
     * split (central role) link resets the negotiated subrating to factor 1. */
    LOG_INF("Conn params [%s %s]: interval=%d.%02dms, latency=%d, timeout=%dms", role, addr_str,
            (interval * 5) / 4, ((interval * 5) % 4) * 25, latency, timeout * 10);
}

#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
static void phy_updated_cb(struct bt_conn *conn, struct bt_conn_le_phy_info *param) {
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr_str, sizeof(addr_str));
    LOG_INF("PHY updated [%s]: tx=%d, rx=%d", addr_str, param->tx_phy, param->rx_phy);
}
#endif

BT_CONN_CB_DEFINE(zmk_subrating_conn_cb) = {
    .subrate_changed = subrate_changed_cb,
    .le_param_updated = le_param_updated_cb,
#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
    .le_phy_updated = phy_updated_cb,
#endif
};
