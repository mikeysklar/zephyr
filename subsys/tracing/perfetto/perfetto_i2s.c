/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <stdio.h>

#include "perfetto_encoder.h"
#include "perfetto_i2s.h"

/*
 * Generate I2S track information from device tree.
 * Each I2S device gets Left and Right counter tracks.
 */

#define DT_DRV_COMPAT zephyr_i2s_sdl

struct i2s_track_info {
	const struct device *dev;
	const char *name;
	uint64_t track_uuid_base;
};

#define I2S_TRACK_INFO(inst) \
	{ \
		.dev = DEVICE_DT_GET(DT_DRV_INST(inst)), \
		.name = DT_NODE_FULL_NAME(DT_DRV_INST(inst)), \
		.track_uuid_base = I2S_TRACK_UUID_BASE + \
				   ((uint64_t)(inst) << 4), \
	},

#if DT_HAS_COMPAT_STATUS_OKAY(zephyr_i2s_sdl)
static const struct i2s_track_info i2s_devices[] = {
	DT_INST_FOREACH_STATUS_OKAY(I2S_TRACK_INFO)
};

#define NUM_I2S_DEVICES ARRAY_SIZE(i2s_devices)
#else
#define NUM_I2S_DEVICES 0
#endif

static bool i2s_tracks_initialized;

/* Sub-track offsets within each device's UUID range */
#define I2S_LEFT_TRACK_OFFSET  0
#define I2S_RIGHT_TRACK_OFFSET 1
#define I2S_GROUP_OFFSET       2

static int find_device_index(const struct device *dev)
{
#if DT_HAS_COMPAT_STATUS_OKAY(zephyr_i2s_sdl)
	for (size_t i = 0; i < NUM_I2S_DEVICES; i++) {
		if (i2s_devices[i].dev == dev) {
			return (int)i;
		}
	}
#endif
	return -1;
}

void perfetto_i2s_init_tracks(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(zephyr_i2s_sdl)
	char track_name[32];

	if (i2s_tracks_initialized) {
		return;
	}

	for (size_t i = 0; i < NUM_I2S_DEVICES; i++) {
		const struct i2s_track_info *info = &i2s_devices[i];
		uint64_t group_uuid = info->track_uuid_base + I2S_GROUP_OFFSET;
		uint64_t left_uuid = info->track_uuid_base + I2S_LEFT_TRACK_OFFSET;
		uint64_t right_uuid = info->track_uuid_base + I2S_RIGHT_TRACK_OFFSET;

		/* Create group track for this I2S device under the I2S group */
		perfetto_emit_track_descriptor(group_uuid, I2S_GROUP_TRACK_UUID,
					       info->name);

		/* Create Left counter track */
		snprintf(track_name, sizeof(track_name), "%s.Left", info->name);
		perfetto_emit_counter_track_descriptor(left_uuid, group_uuid,
						       track_name,
						       PERFETTO_COUNTER_UNIT_COUNT);
		perfetto_emit_counter(left_uuid, 0);

		/* Create Right counter track */
		snprintf(track_name, sizeof(track_name), "%s.Right", info->name);
		perfetto_emit_counter_track_descriptor(right_uuid, group_uuid,
						       track_name,
						       PERFETTO_COUNTER_UNIT_COUNT);
		perfetto_emit_counter(right_uuid, 0);
	}

	i2s_tracks_initialized = true;
#endif
}

/**
 * Extract a signed sample value from a frame at the given byte offset.
 */
static int64_t extract_sample(const uint8_t *frame, uint8_t word_size)
{
	switch (word_size) {
	case 8:
		return (int64_t)(int8_t)frame[0];
	case 16:
		return (int64_t)(int16_t)(frame[0] | (frame[1] << 8));
	case 24:
		return (int64_t)(int32_t)((frame[0] << 8) |
					   (frame[1] << 16) |
					   (frame[2] << 24)) >> 8;
	case 32:
		return (int64_t)(int32_t)(frame[0] | (frame[1] << 8) |
					  (frame[2] << 16) | (frame[3] << 24));
	default:
		return 0;
	}
}

void perfetto_i2s_emit_samples(const struct device *dev, const void *buffer,
			       size_t size, uint8_t word_size, uint8_t channels,
			       uint32_t sample_rate, uint64_t base_timestamp_ns)
{
#if DT_HAS_COMPAT_STATUS_OKAY(zephyr_i2s_sdl)
	if (!i2s_tracks_initialized || sample_rate == 0) {
		return;
	}

	int dev_idx = find_device_index(dev);
	if (dev_idx < 0) {
		return;
	}

	const struct i2s_track_info *info = &i2s_devices[dev_idx];
	uint64_t left_uuid = info->track_uuid_base + I2S_LEFT_TRACK_OFFSET;
	uint64_t right_uuid = info->track_uuid_base + I2S_RIGHT_TRACK_OFFSET;

	uint8_t bytes_per_sample = word_size / 8;
	uint8_t frame_size = bytes_per_sample * channels;

	if (frame_size == 0 || size < frame_size) {
		return;
	}

	size_t num_frames = size / frame_size;

	/* Nanoseconds per frame = 1e9 / sample_rate */
	uint64_t ns_per_frame = 1000000000ULL / sample_rate;

	const uint8_t *buf = (const uint8_t *)buffer;

	for (size_t f = 0; f < num_frames; f++) {
		const uint8_t *frame = buf + f * frame_size;
		int64_t left_val = extract_sample(frame, word_size);
		int64_t right_val;

		if (channels == 2) {
			right_val = extract_sample(frame + bytes_per_sample,
						   word_size);
		} else {
			right_val = left_val;
		}

		uint64_t ts = base_timestamp_ns + f * ns_per_frame;
		perfetto_emit_counter_at(left_uuid, left_val, ts);
		perfetto_emit_counter_at(right_uuid, right_val, ts);
	}
#endif
}
