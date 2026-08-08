# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT

board_runner_args(minichlink "--dt-flash=y")
board_runner_args(wlink "--chip=CH32X035")
board_runner_args(wchisp)
include(${ZEPHYR_BASE}/boards/common/minichlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/wlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/wchisp.board.cmake)
