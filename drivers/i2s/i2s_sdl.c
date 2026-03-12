/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDL based emulated I2S driver for native_sim.
 */

#define DT_DRV_COMPAT zephyr_i2s_sdl

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <soc.h>

#include "i2s_sdl_bottom.h"
#include "cmdline.h"
#include "irq_ctrl.h"
#include "nsi_cpu0_interrupts.h"

#ifdef CONFIG_TRACING_PERFETTO
#include "perfetto_i2s.h"
#endif

LOG_MODULE_REGISTER(i2s_sdl, CONFIG_I2S_LOG_LEVEL);

static bool i2s_sdl_earless;
static const char *i2s_sdl_capture_path;

/* TX queue entry: buffer pointer + size */
struct i2s_sdl_tx_entry {
	void *buffer;
	size_t size;
};

#define TX_QUEUE_LEN 8

enum i2s_sdl_state {
	I2S_SDL_STATE_NOT_READY,
	I2S_SDL_STATE_READY,
	I2S_SDL_STATE_RUNNING,
	I2S_SDL_STATE_STOPPING,
	I2S_SDL_STATE_ERROR,
};

struct i2s_sdl_data {
	enum i2s_sdl_state state;
	struct i2s_config tx_cfg;
	bool tx_configured;
	uint32_t sdl_audio_device_id;

	/* TX queue for buffers from i2s_write() */
	struct k_msgq tx_queue;
	char tx_queue_buf[TX_QUEUE_LEN * sizeof(struct i2s_sdl_tx_entry)];

	/* Thread for draining TX queue */
	struct k_thread tx_thread;
	K_KERNEL_STACK_MEMBER(tx_thread_stack, 2048);
	bool tx_thread_running;
	struct k_sem tx_sem; /* signaled when new data available or stop requested */

	/* Signaled by the ISR when the bottom layer finishes a buffer */
	struct k_sem buffer_done_sem;
};

struct i2s_sdl_config {
	const char *dev_name;
};

static void i2s_sdl_tx_thread(void *p1, void *p2, void *p3);

/* ------------------------------------------------------------------ */
/* Buffer completion: bottom → IRQ → ISR                               */
/* ------------------------------------------------------------------ */

/* Pending completion info, written by the bottom-layer callback
 * (HW model context) and consumed by the ISR.
 */
static volatile struct {
	const struct device *dev;
	void *buffer;
	size_t size;
	uint64_t start_time_us;
	bool valid;
} i2s_pending_done;

/**
 * Bottom-layer callback — runs in NSI HW-event context.
 * Stores completion info and raises an IRQ so the real work
 * (slab free, perfetto trace, sem give) happens in ISR context.
 */
static void i2s_buffer_done_cb(void *user_data, void *buffer,
			       size_t size, uint64_t start_time_us)
{
	i2s_pending_done.dev = (const struct device *)user_data;
	i2s_pending_done.buffer = buffer;
	i2s_pending_done.size = size;
	i2s_pending_done.start_time_us = start_time_us;
	i2s_pending_done.valid = true;
	hw_irq_ctrl_set_irq(I2S_SDL_IRQ);
}

static void i2s_sdl_isr(const void *arg)
{
	ARG_UNUSED(arg);

	while (i2s_pending_done.valid) {
		const struct device *dev = i2s_pending_done.dev;
		struct i2s_sdl_data *data = dev->data;
		void *buffer = i2s_pending_done.buffer;
		size_t size = i2s_pending_done.size;
		uint64_t start_us = i2s_pending_done.start_time_us;

		i2s_pending_done.valid = false;

#ifdef CONFIG_TRACING_PERFETTO
		if (start_us > 0) {
			perfetto_i2s_emit_samples(dev, buffer, size,
						  data->tx_cfg.word_size,
						  data->tx_cfg.channels,
						  data->tx_cfg.frame_clk_freq,
						  start_us * 1000ULL);
		}
#endif

		/* Free buffer back to mem_slab */
		if (data->tx_cfg.mem_slab != NULL) {
			k_mem_slab_free(data->tx_cfg.mem_slab, buffer);
		}

		/* Signal the TX thread that this buffer is done */
		k_sem_give(&data->buffer_done_sem);
	}
}

/* ------------------------------------------------------------------ */
/* I2S driver API                                                      */
/* ------------------------------------------------------------------ */

static void free_tx_queue(struct i2s_sdl_data *data)
{
	struct i2s_sdl_tx_entry entry;

	while (k_msgq_get(&data->tx_queue, &entry, K_NO_WAIT) == 0) {
		if (data->tx_cfg.mem_slab != NULL) {
			k_mem_slab_free(data->tx_cfg.mem_slab, entry.buffer);
		}
	}
}

static int i2s_sdl_configure(const struct device *dev, enum i2s_dir dir,
			     const struct i2s_config *cfg)
{
	struct i2s_sdl_data *data = dev->data;

	if (dir != I2S_DIR_TX) {
		LOG_ERR("Only TX direction supported");
		return -ENOTSUP;
	}

	if (cfg == NULL) {
		/* Deconfigure */
		if (data->state == I2S_SDL_STATE_RUNNING ||
		    data->state == I2S_SDL_STATE_STOPPING) {
			return -EBUSY;
		}
		data->tx_configured = false;
		data->state = I2S_SDL_STATE_NOT_READY;
		return 0;
	}

	if (data->state == I2S_SDL_STATE_RUNNING) {
		return -EAGAIN;
	}

	if (data->state == I2S_SDL_STATE_STOPPING) {
		return -EAGAIN;
	}

	if (cfg->word_size != 8 && cfg->word_size != 16 &&
	    cfg->word_size != 24 && cfg->word_size != 32) {
		LOG_ERR("Unsupported word size: %u", cfg->word_size);
		return -EINVAL;
	}

	if (cfg->channels != 1 && cfg->channels != 2) {
		LOG_ERR("Unsupported channel count: %u", cfg->channels);
		return -EINVAL;
	}

	if (cfg->frame_clk_freq == 0) {
		LOG_ERR("Sample rate must be non-zero");
		return -EINVAL;
	}

	/* Close previous SDL audio device if any */
	if (data->sdl_audio_device_id != 0) {
		i2s_sdl_cleanup_bottom(data->sdl_audio_device_id);
		data->sdl_audio_device_id = 0;
	}

	/* Store config */
	memcpy(&data->tx_cfg, cfg, sizeof(*cfg));
	data->tx_configured = true;

	/* Set capture path before opening SDL audio */
	i2s_sdl_set_capture_path(i2s_sdl_capture_path);

	/* Open SDL audio device */
	struct i2s_sdl_init_params params = {
		.sample_rate = cfg->frame_clk_freq,
		.channels = cfg->channels,
		.bits_per_sample = cfg->word_size,
		.earless = i2s_sdl_earless,
		.audio_device_id = &data->sdl_audio_device_id,
	};

	int ret = i2s_sdl_init_bottom(&params);
	if (ret != 0) {
		LOG_ERR("Failed to init SDL audio");
		data->state = I2S_SDL_STATE_ERROR;
		return -EIO;
	}

	/* Register completion callback (dev pointer as user_data) */
	i2s_sdl_set_done_callback(i2s_buffer_done_cb, (void *)dev);

	/* Reset TX queue */
	k_msgq_purge(&data->tx_queue);

	data->state = I2S_SDL_STATE_READY;
	return 0;
}

static const struct i2s_config *i2s_sdl_config_get(const struct device *dev, enum i2s_dir dir)
{
	struct i2s_sdl_data *data = dev->data;

	if (dir != I2S_DIR_TX || !data->tx_configured) {
		return NULL;
	}

	return &data->tx_cfg;
}

static uint32_t compute_buffer_duration_us(size_t size,
					   const struct i2s_config *cfg)
{
	uint32_t bytes_per_frame = (cfg->word_size / 8) * cfg->channels;

	if (bytes_per_frame == 0 || cfg->frame_clk_freq == 0) {
		return 0;
	}

	uint32_t num_frames = size / bytes_per_frame;

	return (uint64_t)num_frames * 1000000ULL / cfg->frame_clk_freq;
}

static int i2s_sdl_trigger(const struct device *dev, enum i2s_dir dir, enum i2s_trigger_cmd cmd)
{
	struct i2s_sdl_data *data = dev->data;

	if (dir != I2S_DIR_TX) {
		return -ENOTSUP;
	}

	switch (cmd) {
	case I2S_TRIGGER_START:
		if (data->state != I2S_SDL_STATE_READY) {
			LOG_ERR("Cannot start: not in READY state (state=%d)", data->state);
			return -EIO;
		}
		data->state = I2S_SDL_STATE_RUNNING;
		data->tx_thread_running = true;

		/* Start the TX drain thread */
		k_thread_create(&data->tx_thread, data->tx_thread_stack,
				K_KERNEL_STACK_SIZEOF(data->tx_thread_stack),
				i2s_sdl_tx_thread,
				(void *)dev, NULL, NULL,
				5, 0, K_NO_WAIT);
		k_thread_name_set(&data->tx_thread, "i2s_sdl_tx");

		/* Start bottom-layer timed playback */
		i2s_sdl_start_bottom(data->sdl_audio_device_id);
		break;

	case I2S_TRIGGER_STOP:
		if (data->state != I2S_SDL_STATE_RUNNING) {
			return -EIO;
		}
		data->state = I2S_SDL_STATE_STOPPING;
		/* Signal the TX thread to drain and stop */
		k_sem_give(&data->tx_sem);
		break;

	case I2S_TRIGGER_DRAIN:
		if (data->state != I2S_SDL_STATE_RUNNING) {
			return -EIO;
		}
		data->state = I2S_SDL_STATE_STOPPING;
		k_sem_give(&data->tx_sem);
		break;

	case I2S_TRIGGER_DROP:
		if (data->state != I2S_SDL_STATE_RUNNING &&
		    data->state != I2S_SDL_STATE_STOPPING) {
			return -EIO;
		}
		data->tx_thread_running = false;
		k_sem_give(&data->tx_sem);
		k_sem_give(&data->buffer_done_sem);

		/* Wait for TX thread to exit */
		k_thread_join(&data->tx_thread, K_MSEC(1000));

		/* Discard all queued buffers (both msgq and bottom layer) */
		free_tx_queue(data);
		i2s_sdl_drop_buffers();

		/* Stop SDL audio */
		i2s_sdl_stop_bottom(data->sdl_audio_device_id);

		data->state = I2S_SDL_STATE_READY;
		break;

	case I2S_TRIGGER_PREPARE:
		if (data->state != I2S_SDL_STATE_ERROR) {
			return -EIO;
		}
		free_tx_queue(data);
		data->state = I2S_SDL_STATE_READY;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int i2s_sdl_read(const struct device *dev, void **mem_block, size_t *size)
{
	return -ENOTSUP;
}

static int i2s_sdl_write(const struct device *dev, void *mem_block, size_t size)
{
	struct i2s_sdl_data *data = dev->data;

	if (data->state != I2S_SDL_STATE_READY &&
	    data->state != I2S_SDL_STATE_RUNNING) {
		LOG_ERR("Cannot write: not in READY or RUNNING state (state=%d)", data->state);
		return -EIO;
	}

	struct i2s_sdl_tx_entry entry = {
		.buffer = mem_block,
		.size = size,
	};

	k_timeout_t timeout = (data->tx_cfg.timeout == 0)
		? K_NO_WAIT
		: K_MSEC(data->tx_cfg.timeout);

	int ret = k_msgq_put(&data->tx_queue, &entry, timeout);
	if (ret != 0) {
		LOG_ERR("TX queue full");
		return -EAGAIN;
	}

	/* Signal the TX thread */
	k_sem_give(&data->tx_sem);

	return 0;
}

static void i2s_sdl_tx_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = (const struct device *)p1;
	struct i2s_sdl_data *data = dev->data;
	int outstanding = 0;

	while (data->tx_thread_running) {
		struct i2s_sdl_tx_entry entry;

		/* Submit all available buffers to the bottom layer */
		while (k_msgq_get(&data->tx_queue, &entry, K_NO_WAIT) == 0) {
			uint32_t duration_us = compute_buffer_duration_us(
				entry.size, &data->tx_cfg);
			i2s_sdl_submit_buffer(data->sdl_audio_device_id,
					      entry.buffer, entry.size,
					      duration_us);
			outstanding++;
		}

		if (outstanding > 0) {
			/* Wait for one buffer completion, then loop to
			 * submit any new buffers that arrived meanwhile.
			 */
			k_sem_take(&data->buffer_done_sem, K_FOREVER);
			outstanding--;
		} else {
			/* Nothing outstanding — wait for new data or stop */
			k_sem_take(&data->tx_sem, K_FOREVER);
		}

		if (!data->tx_thread_running) {
			break;
		}

		/* If stopping and all outstanding buffers are done */
		if (data->state == I2S_SDL_STATE_STOPPING &&
		    outstanding == 0) {
			i2s_sdl_stop_bottom(data->sdl_audio_device_id);
			data->state = I2S_SDL_STATE_READY;
			data->tx_thread_running = false;
			break;
		}
	}
}

static int i2s_sdl_init(const struct device *dev)
{
	struct i2s_sdl_data *data = dev->data;

	data->state = I2S_SDL_STATE_NOT_READY;
	data->tx_configured = false;
	data->sdl_audio_device_id = 0;
	data->tx_thread_running = false;

	k_msgq_init(&data->tx_queue, data->tx_queue_buf,
		     sizeof(struct i2s_sdl_tx_entry), TX_QUEUE_LEN);
	k_sem_init(&data->tx_sem, 0, K_SEM_MAX_LIMIT);
	k_sem_init(&data->buffer_done_sem, 0, K_SEM_MAX_LIMIT);

	/* Connect and enable the I2S completion IRQ */
	IRQ_CONNECT(I2S_SDL_IRQ, 0, i2s_sdl_isr, NULL, 0);
	irq_enable(I2S_SDL_IRQ);

	return 0;
}

static DEVICE_API(i2s, i2s_sdl_driver_api) = {
	.configure = i2s_sdl_configure,
	.config_get = i2s_sdl_config_get,
	.trigger = i2s_sdl_trigger,
	.read = i2s_sdl_read,
	.write = i2s_sdl_write,
};

#define I2S_SDL_DEFINE(inst)                                                   \
	static struct i2s_sdl_data i2s_sdl_data_##inst;                        \
	static const struct i2s_sdl_config i2s_sdl_config_##inst = {           \
		.dev_name = DT_NODE_FULL_NAME(DT_DRV_INST(inst)),             \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(inst, i2s_sdl_init, NULL,                        \
			      &i2s_sdl_data_##inst,                            \
			      &i2s_sdl_config_##inst,                          \
			      POST_KERNEL, CONFIG_I2S_INIT_PRIORITY,           \
			      &i2s_sdl_driver_api);

DT_INST_FOREACH_STATUS_OKAY(I2S_SDL_DEFINE)

/* Command-line options */
static void i2s_sdl_native_options(void)
{
	static struct args_struct_t args[] = {
		{
			.is_switch = true,
			.option = "i2s_earless",
			.type = 'b',
			.dest = (void *)&i2s_sdl_earless,
			.descript = "Disable SDL audio output (earless mode)"
		},
		{
			.option = "i2s_capture",
			.name = "path",
			.type = 's',
			.dest = (void *)&i2s_sdl_capture_path,
			.descript = "Capture I2S audio to a WAV file",
		},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(args);
}

NATIVE_TASK(i2s_sdl_native_options, PRE_BOOT_1, 1);
