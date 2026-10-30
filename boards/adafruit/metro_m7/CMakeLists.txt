# Copyright (c) 2026 Adafruit Industries LLC
# SPDX-License-Identifier: Apache-2.0

# The flash boot header is the only board source. An image that a bootloader
# such as MCUboot starts has no boot header, so it gets no board library.
if(CONFIG_NXP_IMXRT_BOOT_HEADER AND CONFIG_BOOT_FLEXSPI_NOR)
  zephyr_library()
  zephyr_library_sources(flexspi_nor_config.c)
endif()
