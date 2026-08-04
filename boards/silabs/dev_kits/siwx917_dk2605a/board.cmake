# Copyright (c) 2025 Silicon Laboratories Inc.
# SPDX-License-Identifier: Apache-2.0

# Pin the flash artifact to zephyr.rps. Only the RPS container boots this SoC:
# it carries the boot header/CRC and commander routes it through the
# bootloader's upgrade path. Both raw-image alternatives were tried on
# hardware 2026-08-04 and neither boots (hex: parsed addresses, no header;
# bin at the devicetree flash address: no header either). Without this pin the
# runner silently prefers whichever of .hex/.bin exists -- and
# CONFIG_BUILD_OUTPUT_HEX (needed for release artifacts) makes .hex exist.
board_runner_args(silabs_commander "--device=SiWG917M111MGTBA" "--file=${PROJECT_BINARY_DIR}/zephyr.rps")
include(${ZEPHYR_BASE}/boards/common/silabs_commander.board.cmake)

# It is not possible to load/flash a firmware using JLink, but it is possible to
# debug a firmware with:
#    west attach -r jlink
# Once started, it should be possible to reset the device with "monitor reset"
board_runner_args(jlink "--device=Si917" "--speed=10000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
