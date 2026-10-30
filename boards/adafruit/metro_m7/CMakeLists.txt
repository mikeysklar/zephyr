# Copyright (c) 2026 Adafruit Industries LLC
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

if(CONFIG_NXP_IMXRT_BOOT_HEADER AND CONFIG_BOOT_FLEXSPI_NOR)
  zephyr_library_sources(flexspi_nor_config.c)
endif()
