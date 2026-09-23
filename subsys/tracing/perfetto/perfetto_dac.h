/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SUBSYS_TRACING_PERFETTO_DAC_H
#define SUBSYS_TRACING_PERFETTO_DAC_H

#include <zephyr/device.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize DAC counter tracks
 *
 * Creates counter track descriptors for each emulated DAC instance and
 * channel (named "<node>.<channel>"). Called from perfetto_start().
 */
void perfetto_dac_init_tracks(void);

/**
 * @brief Emit a DAC write as a counter event on the channel's track
 *
 * @param dev DAC device pointer
 * @param channel Channel number that was written
 * @param value Value that was written (already scaled by the driver)
 */
void perfetto_dac_emit_write(const struct device *dev, uint8_t channel,
			     int64_t value);

#ifdef __cplusplus
}
#endif

#endif /* SUBSYS_TRACING_PERFETTO_DAC_H */
