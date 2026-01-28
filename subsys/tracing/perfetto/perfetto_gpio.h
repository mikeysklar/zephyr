/*
 * Copyright (c) 2024 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SUBSYS_TRACING_PERFETTO_GPIO_H
#define SUBSYS_TRACING_PERFETTO_GPIO_H

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize GPIO counter tracks
 *
 * Creates counter track descriptors for each GPIO pin based on device tree.
 * Called automatically from perfetto_start().
 */
void perfetto_gpio_init_tracks(void);

#ifdef __cplusplus
}
#endif

#endif /* SUBSYS_TRACING_PERFETTO_GPIO_H */
