# Copyright (c) 2026 Adafruit Industries
# SPDX-License-Identifier: Apache-2.0

zephyr_sources(
  soc_irq.S
  vector.S
)

zephyr_include_directories(.)

zephyr_library_sources_ifdef(CONFIG_ARCH_HAS_THREAD_ABORT thread_abort.c)
zephyr_library_include_directories(
  ${ZEPHYR_BASE}/kernel/include
  ${ZEPHYR_BASE}/arch/${ARCH}/include
)
