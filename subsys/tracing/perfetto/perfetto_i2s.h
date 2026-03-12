/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SUBSYS_TRACING_PERFETTO_I2S_H
#define SUBSYS_TRACING_PERFETTO_I2S_H

#include <zephyr/device.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2S counter tracks
 *
 * Creates counter track descriptors for each I2S device (Left and Right channels).
 * Called from perfetto_start().
 */
void perfetto_i2s_init_tracks(void);

/**
 * @brief Emit I2S sample values as counter events
 *
 * Extracts left and right channel sample values from the buffer and emits
 * them as Perfetto counter events with timestamps spread across the buffer's
 * real-time duration based on sample_rate.
 *
 * @param dev I2S device pointer
 * @param buffer Audio buffer
 * @param size Buffer size in bytes
 * @param word_size Bits per sample (8, 16, 24, 32)
 * @param channels Number of channels (1 or 2)
 * @param sample_rate Sample rate in Hz (frames per second)
 * @param base_timestamp_ns Start timestamp in nanoseconds for this buffer
 */
void perfetto_i2s_emit_samples(const struct device *dev, const void *buffer,
			       size_t size, uint8_t word_size, uint8_t channels,
			       uint32_t sample_rate, uint64_t base_timestamp_ns);

#ifdef __cplusplus
}
#endif

#endif /* SUBSYS_TRACING_PERFETTO_I2S_H */
