# Copyright (c) 2026 Adafruit Industries LLC
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=mimxrt1010")
board_runner_args(jlink "--device=MIMXRT1011xxx5A")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
