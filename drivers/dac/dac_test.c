/*
 * Copyright (c) 2024 TOKITA Hiroshi
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Emulated DAC for native_sim testing. The CircuitPython analogio.AnalogOut
 * code writes full-scale 16-bit values; writes are recorded as counter
 * events on the "<node>.<channel>" perfetto track so tests can assert on
 * them. With a vref-mv reference in the devicetree the tracks carry
 * millivolts (converted from the code through the reference, like the ADC
 * emul's millivolt inputs); without one they carry the raw code. The
 * --dac-resolution runtime option quantizes written codes to fewer bits
 * first, letting tests exercise the full-scale -> DAC output rounding of a
 * real DAC peripheral.
 */

#define DT_DRV_COMPAT vnd_dac

#include <errno.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/dac.h>

#include <soc.h>

#ifdef CONFIG_BOARD_NATIVE_SIM
#include "cmdline.h"
#endif

#ifdef CONFIG_TRACING_PERFETTO
#include "perfetto_dac.h"
#endif

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vnd_dac, CONFIG_LOG_DEFAULT_LEVEL);

/* The emulated hardware accepts any channel id; bound the bookkeeping. */
#define VND_DAC_MAX_CHANNELS 8

struct vnd_dac_channel {
	uint8_t resolution;
	bool configured;
};

struct vnd_dac_data {
	struct vnd_dac_channel channels[VND_DAC_MAX_CHANNELS];
};

struct vnd_dac_config {
	/* Reference voltage in mV; 0 records the raw code on the tracks. */
	uint32_t vref_mv;
};

#ifdef CONFIG_BOARD_NATIVE_SIM
/* Resolution set at runtime through --dac-resolution (0: use the
 * resolution the channel was configured with). Shared by all instances. */
static uint8_t vnd_dac_runtime_resolution;

static void cmd_dac_resolution(char *argv, int offset)
{
	vnd_dac_runtime_resolution = (uint8_t)strtoul(argv + offset, NULL, 0);
}

static struct args_struct_t vnd_dac_options[] = {
	{
		.option = "dac-resolution",
		.name = "bits",
		.type = 's',
		.call_when_found = cmd_dac_resolution,
		.descript = "Scale DAC writes to this many bits (default: "
			    "the resolution each channel was set up with)"
	},
	ARG_TABLE_ENDMARKER,
};

static void vnd_dac_register_options(void)
{
	native_add_command_line_opts(vnd_dac_options);
}

NATIVE_TASK(vnd_dac_register_options, PRE_BOOT_1, 1);
#endif /* CONFIG_BOARD_NATIVE_SIM */

/* Scale a full-scale value down to (or up from) fewer bits, rounding like a
 * real DAC's converter: (value * out_max + in_max / 2) / in_max. */
static uint32_t vnd_dac_scale(uint32_t value, uint8_t bits)
{
	uint64_t in_max = (1ULL << 16) - 1;
	uint64_t out_max = ((uint64_t)1 << bits) - 1;

	if (value > in_max) {
		value = in_max;
	}

	return (uint32_t)((value * out_max + in_max / 2) / in_max);
}

static int vnd_dac_channel_setup(const struct device *dev,
				 const struct dac_channel_cfg *channel_cfg)
{
	struct vnd_dac_data *data = dev->data;

	if (channel_cfg->channel_id >= VND_DAC_MAX_CHANNELS) {
		LOG_ERR("unsupported channel %u", channel_cfg->channel_id);
		return -EINVAL;
	}

	if (channel_cfg->resolution == 0 || channel_cfg->resolution > 32) {
		LOG_ERR("unsupported resolution %u", channel_cfg->resolution);
		return -EINVAL;
	}

	data->channels[channel_cfg->channel_id].resolution =
		channel_cfg->resolution;
	data->channels[channel_cfg->channel_id].configured = true;

	return 0;
}

static int vnd_dac_write_value(const struct device *dev, uint8_t channel,
			       uint32_t value)
{
	struct vnd_dac_data *data = dev->data;

	if (channel >= VND_DAC_MAX_CHANNELS ||
	    !data->channels[channel].configured) {
		LOG_ERR("channel %u not configured", channel);
		return -EINVAL;
	}

	uint8_t bits = data->channels[channel].resolution;
#ifdef CONFIG_BOARD_NATIVE_SIM
	if (vnd_dac_runtime_resolution != 0) {
		bits = vnd_dac_runtime_resolution;
	}
#endif

	uint32_t output = value;
	if (bits < 16) {
		output = vnd_dac_scale(value, bits);
	} else if (bits > 16 && value <= UINT16_MAX) {
		output = vnd_dac_scale(value, bits);
	}

#ifdef CONFIG_TRACING_PERFETTO
	const struct vnd_dac_config *config = dev->config;
	uint64_t recorded = output;
	if (config->vref_mv != 0) {
		uint64_t out_max = bits >= 32 ? UINT32_MAX : ((1ULL << bits) - 1);
		/* Converter rounding: out_max / 2 in the numerator, matching
		 * vnd_dac_scale()'s full-range rounding. */
		recorded = ((uint64_t)output * config->vref_mv + out_max / 2) /
			   out_max;
	}
	perfetto_dac_emit_write(dev, channel, (int64_t)recorded);
#endif

	return 0;
}

static DEVICE_API(dac, vnd_dac_driver_api) = {
	.channel_setup = vnd_dac_channel_setup,
	.write_value = vnd_dac_write_value,
};

static int vnd_dac_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

#define VND_DAC_INIT(index)                                                                        \
	static const struct vnd_dac_config vnd_dac_config_##index = {                              \
		.vref_mv = DT_INST_PROP_OR(index, vref_mv, 0),                                     \
	};                                                                                         \
	static struct vnd_dac_data vnd_dac_data_##index;                                           \
	DEVICE_DT_INST_DEFINE(index, &vnd_dac_init, NULL, &vnd_dac_data_##index,                   \
			      &vnd_dac_config_##index, POST_KERNEL,                                \
			      CONFIG_DAC_INIT_PRIORITY, &vnd_dac_driver_api);

DT_INST_FOREACH_STATUS_OKAY(VND_DAC_INIT)
