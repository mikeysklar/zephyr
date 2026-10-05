/*
 * Copyright 2020 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * This driver creates fake SPI buses which can contain emulated devices,
 * implemented by a separate emulation driver. The API between this driver and
 * its emulators is defined by struct spi_emul_driver_api.
 */

#define DT_DRV_COMPAT zephyr_spi_emul_controller

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_emul_ctlr);

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi.h>
#include "spi_rtio.h"
#include <zephyr/drivers/spi_emul.h>

/** Working data for the device */
struct spi_emul_data {
	/* List of struct spi_emul associated with the device */
	sys_slist_t emuls;
	/* SPI host configuration */
	uint32_t config;
#ifdef CONFIG_SPI_ASYNC
	/* Completion of the async transfer in flight */
	const struct device *dev;
	struct k_work_delayable done;
	spi_callback_t cb;
	void *userdata;
#endif
};

uint32_t spi_emul_get_config(const struct device *dev)
{
	struct spi_emul_data *data = dev->data;

	return data->config;
}

/**
 * Find an emulator for a SPI bus
 *
 * At present only a single emulator is supported on the bus, since we do not
 * support chip selects, despite there being a chipsel field. It cannot be
 * implemented until we have a GPIO emulator.
 *
 * @param dev SPI emulation controller device
 * @param chipsel Chip-select value
 * @return emulator to use
 * @return NULL if not found
 */
static struct spi_emul *spi_emul_find(const struct device *dev, unsigned int chipsel)
{
	struct spi_emul_data *data = dev->data;
	sys_snode_t *node;

	SYS_SLIST_FOR_EACH_NODE(&data->emuls, node) {
		struct spi_emul *emul;

		emul = CONTAINER_OF(node, struct spi_emul, node);
		if (emul->chipsel == chipsel) {
			return emul;
		}
	}

	return NULL;
}

static int spi_emul_io(const struct device *dev, const struct spi_config *config,
		       const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
	struct spi_emul *emul;
	const struct spi_emul_api *api;
	int ret;

	emul = spi_emul_find(dev, config->peripheral);
	if (!emul) {
		return -EIO;
	}

	api = emul->api;
	__ASSERT_NO_MSG(emul->api);
	__ASSERT_NO_MSG(emul->api->io);

	if (emul->mock_api != NULL && emul->mock_api->io != NULL) {
		ret = emul->mock_api->io(emul->target, config, tx_bufs, rx_bufs);
		if (ret != -ENOSYS) {
			return ret;
		}
	}

	return api->io(emul->target, config, tx_bufs, rx_bufs);
}

#ifdef CONFIG_SPI_ASYNC
static void spi_emul_done(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct spi_emul_data *data = CONTAINER_OF(dwork, struct spi_emul_data, done);

	if (data->cb != NULL) {
		data->cb(data->dev, 0, data->userdata);
	}
}

static size_t spi_emul_buf_set_len(const struct spi_buf_set *bufs)
{
	size_t len = 0;

	if (bufs != NULL) {
		for (size_t i = 0; i < bufs->count; i++) {
			len += bufs->buffers[i].len;
		}
	}

	return len;
}

/**
 * The data moves at once, as with the blocking call, but the completion callback runs later,
 * after the time the bytes would take on the wire, so that callers see an asynchronous transfer.
 */
static int spi_emul_transceive_async(const struct device *dev, const struct spi_config *config,
				     const struct spi_buf_set *tx_bufs,
				     const struct spi_buf_set *rx_bufs, spi_callback_t cb,
				     void *userdata)
{
	struct spi_emul_data *data = dev->data;
	int ret;

	if (k_work_delayable_is_pending(&data->done)) {
		return -EBUSY;
	}

	ret = spi_emul_io(dev, config, tx_bufs, rx_bufs);
	if (ret != 0) {
		return ret;
	}

	size_t len = MAX(spi_emul_buf_set_len(tx_bufs), spi_emul_buf_set_len(rx_bufs));
	k_timeout_t delay = K_NO_WAIT;

	if (config->frequency != 0) {
		delay = K_USEC((uint64_t)len * 8 * USEC_PER_SEC / config->frequency);
	}

	data->cb = cb;
	data->userdata = userdata;
	k_work_schedule(&data->done, delay);

	return 0;
}
#endif /* CONFIG_SPI_ASYNC */

/**
 * @brief This is a no-op stub of the SPI API's `release` method to protect drivers under test
 *        from hitting a segmentation fault when using SPI_LOCK_ON plus spi_release()
 */
static int spi_emul_release(const struct device *dev, const struct spi_config *config)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(config);

	return 0;
}

/**
 * Set up a new emulator and add it to the list
 *
 * @param dev SPI emulation controller device
 */
static int spi_emul_init(const struct device *dev)
{
	struct spi_emul_data *data = dev->data;

	sys_slist_init(&data->emuls);
#ifdef CONFIG_SPI_ASYNC
	data->dev = dev;
	k_work_init_delayable(&data->done, spi_emul_done);
#endif

	return emul_init_for_bus(dev);
}

int spi_emul_register(const struct device *dev, struct spi_emul *emul)
{
	struct spi_emul_data *data = dev->data;
	const char *name = emul->target->dev->name;

	sys_slist_append(&data->emuls, &emul->node);

	LOG_INF("Register emulator '%s' at cs %u\n", name, emul->chipsel);

	return 0;
}

/* Device instantiation */

static DEVICE_API(spi, spi_emul_api) = {
	.transceive = spi_emul_io,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_emul_transceive_async,
#endif
#ifdef CONFIG_SPI_RTIO
	.iodev_submit = spi_rtio_iodev_default_submit,
#endif
	.release = spi_emul_release,
};

#define EMUL_LINK_AND_COMMA(node_id)                                                               \
	{                                                                                          \
		.dev = DEVICE_DT_GET(node_id),                                                     \
	},

#define SPI_EMUL_INIT(n)                                                                           \
	static const struct emul_link_for_bus emuls_##n[] = {                                      \
		DT_FOREACH_CHILD_STATUS_OKAY(DT_DRV_INST(n), EMUL_LINK_AND_COMMA)};                \
	static struct emul_list_for_bus spi_emul_cfg_##n = {                                       \
		.children = emuls_##n,                                                             \
		.num_children = ARRAY_SIZE(emuls_##n),                                             \
	};                                                                                         \
	static struct spi_emul_data spi_emul_data_##n;                                             \
	SPI_DEVICE_DT_INST_DEFINE(n, spi_emul_init, NULL, &spi_emul_data_##n, &spi_emul_cfg_##n,   \
				  POST_KERNEL, CONFIG_SPI_INIT_PRIORITY, &spi_emul_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_EMUL_INIT)
