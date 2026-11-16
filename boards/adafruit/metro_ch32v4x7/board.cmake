# Copyright (c) 2026 Adafruit Industries
# SPDX-License-Identifier: Apache-2.0

# The Board-ID that adaboot reports in INFO_UF2.TXT is the board name.
if(CONFIG_BUILD_OUTPUT_UF2)
  board_runner_args(uf2 "--board-id=${BOARD}")
  include(${ZEPHYR_BASE}/boards/common/uf2.board.cmake)
endif()
