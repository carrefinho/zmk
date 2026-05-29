/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Drives the split link to a 1.25 ms connection interval via the Bluetooth 6.2
 * Shorter Connection Intervals (RCV) Connection Rate Update procedure, pinned at
 * subrate factor 1 (no subrating tiers). Central-only: the central initiates the
 * procedure on each split connection after the page-1 feature exchange (it gates
 * on the peer's SCI Host Support bit).
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 1.25 ms in 125 us units (the RCV floor); supervision timeout in 10 ms units. */
#define SCI_INTERVAL_125US 10U
#define SCI_TIMEOUT_10MS   ((uint16_t)(CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT))

static void sci_request_rate(struct bt_conn *conn, void *data) {
    ARG_UNUSED(data);

    struct bt_conn_info info;

    if (bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE) {
        return;
    }
    /* Only the split links (where we are the LE central) get driven to 1.25 ms. */
    if (info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }

    struct bt_conn_le_conn_rate_param rate = {
        .interval_min_125us = SCI_INTERVAL_125US,
        .interval_max_125us = SCI_INTERVAL_125US,
        .subrate_min = 1U,
        .subrate_max = 1U,
        .max_latency = 0U,
        .continuation_number = 0U,
        .supervision_timeout_10ms = SCI_TIMEOUT_10MS,
        .min_ce_len_125us = 1U,
        .max_ce_len_125us = 1U,
    };
    int err = bt_conn_le_conn_rate_request(conn, &rate);

    LOG_INF("SCI: requested 1.25 ms / factor 1 on split link (err %d)", err);
}

/* After the connection settles, read the peer's page-1 features (SCI Host
 * Support lives there) so the controller will let us initiate the procedure.
 */
static void sci_read_features(struct bt_conn *conn, void *data) {
    ARG_UNUSED(data);

    struct bt_conn_info info;

    if (bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE ||
        info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }

    int err = bt_conn_le_read_all_remote_features(conn, 1U);

    LOG_DBG("SCI: read page-1 features on split link (err %d)", err);
}

static void sci_features_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    bt_conn_foreach(BT_CONN_TYPE_LE, sci_read_features, NULL);
}
static K_WORK_DELAYABLE_DEFINE(sci_features_work, sci_features_work_cb);

static void sci_connected(struct bt_conn *conn, uint8_t err) {
    struct bt_conn_info info;

    if (err != 0U || bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE ||
        info.role != BT_CONN_ROLE_CENTRAL) {
        return;
    }
    /* Give the auto feature/PHY exchange + GATT discovery time to settle. */
    k_work_reschedule(&sci_features_work, K_MSEC(1500));
}

static void sci_remote_feat_complete(struct bt_conn *conn,
                                     struct bt_conn_le_read_all_remote_feat_complete *params) {
    ARG_UNUSED(params);
    sci_request_rate(conn, NULL);
}

static void sci_conn_rate_changed(struct bt_conn *conn, uint8_t status,
                                  const struct bt_conn_le_conn_rate_changed *params) {
    if (status == BT_HCI_ERR_SUCCESS && params != NULL) {
        LOG_INF("SCI: link now at %u us, factor %u", params->interval_us, params->subrate_factor);
    } else {
        LOG_WRN("SCI: conn rate update failed (status 0x%02x)", status);
    }
}

static struct bt_conn_cb sci_conn_callbacks = {
    .connected = sci_connected,
    .read_all_remote_feat_complete = sci_remote_feat_complete,
    .conn_rate_changed = sci_conn_rate_changed,
};

static int zmk_split_sci_init(void) {
    bt_conn_cb_register(&sci_conn_callbacks);
    return 0;
}

SYS_INIT(zmk_split_sci_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
