/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/devicetree.h>

#define ZMK_MATRIX_HAS_TRANSFORM DT_HAS_CHOSEN(zmk_matrix_transform)

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_physical_layout)

#define ZMK_PHYSICAL_LAYOUT_BYTE_ARRAY(node_id)                                                    \
    uint8_t _CONCAT(prop_, node_id)[DT_PROP_LEN(DT_PHANDLE(node_id, transform), map)];

#define ZMK_KEYMAP_LEN                                                                             \
    sizeof(union {DT_FOREACH_STATUS_OKAY(zmk_physical_layout, ZMK_PHYSICAL_LAYOUT_BYTE_ARRAY)})

#elif ZMK_MATRIX_HAS_TRANSFORM

#define ZMK_KEYMAP_TRANSFORM_NODE DT_CHOSEN(zmk_matrix_transform)
#define ZMK_KEYMAP_LEN DT_PROP_LEN(ZMK_KEYMAP_TRANSFORM_NODE, map)

#elif DT_HAS_CHOSEN(zmk_matrix_input)

#define ZMK_MATRIX_INPUT_NODE DT_CHOSEN(zmk_matrix_input)
#if DT_NODE_HAS_PROP(ZMK_MATRIX_INPUT_NODE, row_size)
#define ZMK_MATRIX_ROWS DT_PROP(ZMK_MATRIX_INPUT_NODE, row_size)
#define ZMK_MATRIX_COLS DT_PROP(ZMK_MATRIX_INPUT_NODE, col_size)
#elif DT_NODE_HAS_COMPAT(ZMK_MATRIX_INPUT_NODE, gpio_keys)
#define ZMK_MATRIX_ROWS 1
#define ZMK_MATRIX_COLS DT_CHILD_NUM_STATUS_OKAY(ZMK_MATRIX_INPUT_NODE)
#endif

#define ZMK_KEYMAP_LEN (ZMK_MATRIX_COLS * ZMK_MATRIX_ROWS)

#elif IS_ENABLED(CONFIG_ZMK_KSCAN_LEGACY) && DT_HAS_CHOSEN(zmk_kscan)

#define ZMK_MATRIX_NODE_ID DT_CHOSEN(zmk_kscan)

#if DT_NODE_HAS_PROP(ZMK_MATRIX_NODE_ID, row_gpios)
#define ZMK_MATRIX_ROWS DT_PROP_LEN(ZMK_MATRIX_NODE_ID, row_gpios)
#define ZMK_MATRIX_COLS DT_PROP_LEN(ZMK_MATRIX_NODE_ID, col_gpios)
#elif DT_NODE_HAS_PROP(ZMK_MATRIX_NODE_ID, input_gpios)
#define ZMK_MATRIX_ROWS 1
#define ZMK_MATRIX_COLS DT_PROP_LEN(ZMK_MATRIX_NODE_ID, input_gpios)
#else
#define ZMK_MATRIX_ROWS DT_PROP(ZMK_MATRIX_NODE_ID, rows)
#define ZMK_MATRIX_COLS DT_PROP(ZMK_MATRIX_NODE_ID, columns)
#endif

#define ZMK_KEYMAP_LEN (ZMK_MATRIX_COLS * ZMK_MATRIX_ROWS)

#endif
