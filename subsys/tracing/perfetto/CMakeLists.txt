# Copyright (c) 2024 Adafruit Industries
# SPDX-License-Identifier: Apache-2.0

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

zephyr_library()
zephyr_library_sources(
  perfetto_top.c
  perfetto_encoder.c
)

zephyr_library_sources_ifdef(CONFIG_TRACING_GPIO
  perfetto_gpio.c
)

# Generate nanopb sources from proto file
zephyr_nanopb_sources(zephyr proto/perfetto_trace.proto)

# Include directories - add the build dir for generated proto headers
zephyr_include_directories(.)
zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR})
zephyr_include_directories(
  ${ZEPHYR_BASE}/kernel/include
  ${ARCH_DIR}/${ARCH}/include
)
