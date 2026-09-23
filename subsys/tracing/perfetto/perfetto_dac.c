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
#include "perfetto_dac.h"

/*
 * Generate DAC track information from device tree. Each DAC instance gets a
 * group track and one counter track per channel, named "<node>.<channel>".
 */

#define DT_DRV_COMPAT vnd_dac

/* The emulated hardware accepts any channel id; expose this many tracks. */
#define DAC_EMUL_MAX_CHANNELS 8
#define DAC_GROUP_OFFSET      8

struct dac_track_info {
	const struct device *dev;
	const char *name;
	uint64_t track_uuid_base;
};

#define DAC_TRACK_INFO(inst) \
	{ \
		.dev = DEVICE_DT_GET(DT_DRV_INST(inst)), \
		.name = DT_NODE_FULL_NAME(DT_DRV_INST(inst)), \
		.track_uuid_base = DAC_TRACK_UUID_BASE + \
				   ((uint64_t)(inst) << 4), \
	},

#if DT_HAS_COMPAT_STATUS_OKAY(vnd_dac)
static const struct dac_track_info dac_devices[] = {
	DT_INST_FOREACH_STATUS_OKAY(DAC_TRACK_INFO)
};

#define NUM_DAC_DEVICES ARRAY_SIZE(dac_devices)
#else
#define NUM_DAC_DEVICES 0
#endif

static bool dac_tracks_initialized;

static int find_device_index(const struct device *dev)
{
#if DT_HAS_COMPAT_STATUS_OKAY(vnd_dac)
	for (size_t i = 0; i < NUM_DAC_DEVICES; i++) {
		if (dac_devices[i].dev == dev) {
			return (int)i;
		}
	}
#endif
	return -1;
}

void perfetto_dac_init_tracks(void)
{
#if DT_HAS_COMPAT_STATUS_OKAY(vnd_dac)
	char track_name[32];

	if (dac_tracks_initialized) {
		return;
	}

	for (size_t i = 0; i < NUM_DAC_DEVICES; i++) {
		const struct dac_track_info *info = &dac_devices[i];
		uint64_t group_uuid = info->track_uuid_base + DAC_GROUP_OFFSET;

		/* Create group track for this DAC device under the DAC group */
		perfetto_emit_track_descriptor(group_uuid, DAC_GROUP_TRACK_UUID,
					       info->name);

		for (uint8_t chan = 0; chan < DAC_EMUL_MAX_CHANNELS; chan++) {
			uint64_t track_uuid = info->track_uuid_base + chan;

			snprintf(track_name, sizeof(track_name), "%s.%u",
				 info->name, chan);
			perfetto_emit_counter_track_descriptor(track_uuid,
							       group_uuid,
							       track_name,
							       PERFETTO_COUNTER_UNIT_COUNT);
			perfetto_emit_counter(track_uuid, 0);
		}
	}

	dac_tracks_initialized = true;
#endif
}

void perfetto_dac_emit_write(const struct device *dev, uint8_t channel,
			     int64_t value)
{
#if DT_HAS_COMPAT_STATUS_OKAY(vnd_dac)
	if (!dac_tracks_initialized || channel >= DAC_EMUL_MAX_CHANNELS) {
		return;
	}

	int dev_idx = find_device_index(dev);
	if (dev_idx < 0) {
		return;
	}

	perfetto_emit_counter(dac_devices[dev_idx].track_uuid_base + channel,
			      value);
#endif
}
