/*
 * Copyright (c) 2024 Stephen Boylan (stephen.boylan@beechwoods.com)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <airoc_wifi.h>
#include "airoc_whd_hal_common.h"

#include <bus_protocols/whd_bus_spi_protocol.h>
#include <bus_protocols/whd_spi.h>
#include <whd_wifi_api.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(infineon_airoc_wifi, CONFIG_WIFI_LOG_LEVEL);

/*
 * Shared gSPI bus lock.
 *
 * On the Pico W / Pico 2 W a single gSPI bus carries both WiFi (this WHD WLAN
 * path) and Bluetooth (cybt_shared_bus, driven from
 * drivers/bluetooth/hci/cyw43_bt_whd_glue.c). WHD has no bus mutex of its own;
 * it relies on "only whd_thread touches the bus", which the BT path violates.
 * Without serialization, concurrent WiFi+BT traffic corrupts transfers below
 * the host lock: WiFi association fails and the BT ring indices read back out
 * of range. So bracket every WLAN bus transfer with a lock the BT path also
 * takes. The strong definition lives in cyw43_bt_whd_glue.c (recursive, with
 * priority inheritance); the __weak no-ops here keep a build without
 * CONFIG_BT_CYW43_SHARED_BUS linking unchanged.
 */
__weak void whd_bus_lock(void)
{
}
__weak void whd_bus_unlock(void)
{
}

/*
 * The WL_HOST_WAKE OOB IRQ is shared: the CYW43439 raises it for BT
 * controller->host data as well as for WLAN, and WHD only wakes its own
 * whd_thread. This hook lets a BT transport be notified too. The shared-bus BT
 * driver currently drains on a fixed-interval poll rather than off this IRQ, so
 * the __weak no-op stands; it is kept as a stable seam for a future
 * IRQ-driven RX path.
 */
__weak void whd_bt_notify_irq(void)
{
}

#ifdef __cplusplus
extern "C" {
#endif

struct whd_bus_priv {
	whd_spi_config_t spi_config;
	whd_spi_t spi_obj;
};

static whd_init_config_t init_config_default = {.thread_stack_size = CY_WIFI_THREAD_STACK_SIZE,
						.thread_stack_start = NULL,
						.thread_priority =
							(uint32_t)CY_WIFI_THREAD_PRIORITY,
						.country = CY_WIFI_COUNTRY};

/******************************************************
 *		Function
 ******************************************************/

int airoc_wifi_init_primary(const struct device *dev, whd_interface_t *interface,
				whd_netif_funcs_t *netif_funcs,
				whd_buffer_funcs_t *buffer_if)
{
	struct airoc_wifi_data *data = dev->data;
	const struct airoc_wifi_config *config = dev->config;

	whd_spi_config_t whd_spi_config = {
		.is_spi_normal_mode = WHD_FALSE,
	};

#if DT_INST_NODE_HAS_PROP(0, wifi_host_wake_gpios)
	whd_oob_config_t oob_config = {
		.host_oob_pin = (void *)&config->wifi_host_wake_gpio,
		.dev_gpio_sel = DEFAULT_OOB_PIN,
		.is_falling_edge =
			(CY_WIFI_HOST_WAKE_IRQ_EVENT == GPIO_INT_TRIG_LOW) ? WHD_TRUE : WHD_FALSE,
		.intr_priority = CY_WIFI_OOB_INTR_PRIORITY};
	whd_spi_config.oob_config = oob_config;
#endif
#if defined(SPI_DATA_IRQ_SHARED)
	data->prev_irq_state = 0;
#endif

	/* Pull bus select line low before enabling WiFi chip */
	gpio_pin_configure_dt(&config->bus_select_gpio, GPIO_OUTPUT_INACTIVE);

	if (airoc_wifi_power_on(dev)) {
		LOG_ERR("airoc_wifi_power_on returns fail");
		return -ENODEV;
	}

	if (!spi_is_ready_dt(&config->bus_dev.bus_spi)) {
		LOG_ERR("SPI device is not ready");
		return -ENODEV;
	}

	/* Init wifi host driver (whd) */
	cy_rslt_t whd_ret = whd_init(&data->whd_drv, &init_config_default,
					&resource_ops, buffer_if, netif_funcs);
	if (whd_ret == CY_RSLT_SUCCESS) {
		whd_ret = whd_bus_spi_attach(data->whd_drv, &whd_spi_config,
					    (whd_spi_t)&config->bus_dev.bus_spi);

		if (whd_ret == CY_RSLT_SUCCESS) {
			whd_ret = whd_wifi_on(data->whd_drv, interface);
		}

		if (whd_ret != CY_RSLT_SUCCESS) {
			whd_deinit(*interface);
			return -ENODEV;
		}
	}
	return 0;
}

/*
 * Implement SPI Transfer wrapper
 */

whd_result_t whd_bus_spi_transfer(whd_driver_t whd_driver, const uint8_t *tx, size_t tx_length,
				  uint8_t *rx, size_t rx_length, uint8_t write_fill)
{
	const struct spi_dt_spec *spi_obj = whd_driver->bus_priv->spi_obj;
	int ret;
	whd_result_t whd_ret = WHD_SUCCESS;

	/* Hold the shared gSPI bus lock across the whole transfer,
	 * including the SPI_DATA_IRQ_SHARED pinctrl/IRQ toggle below, so a BT
	 * backplane access can never interleave mid-transfer. Recursive: the BT
	 * path holds it across a cybt sequence and re-enters here.
	 */
	whd_bus_lock();

#if defined(SPI_DATA_IRQ_SHARED)
	ret = whd_bus_spi_irq_enable(whd_driver, false);
	if (ret) {
		LOG_ERR("whd_bus_spi_irq_enable FAIL %d\n", ret);
		whd_ret = WHD_WLAN_SDIO_ERROR;
	}
#endif

	/* In some cases whd_bus_spi_protocol.c places the command at    */
	/* the start of the rx buffer and passes NULL as the tx address, */
	/* reusing the same buffer (and overwriting the tx data).        */
	if (tx == NULL && tx_length > 0 && rx_length >= tx_length) {
		tx = rx;
	}

	const struct spi_buf tx_buf = {.buf = (uint8_t *)tx, .len = tx_length};
	const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
	struct spi_buf rx_buf[2];
	struct spi_buf_set rx_set = {.buffers = rx_buf, .count = 2};

	if (rx != NULL) {
		rx += tx_length;
	}

	if (rx_length >= tx_length) {
		rx_length -= tx_length;
	} else {
		rx_length = 0;
	}

	if (spi_obj->config.operation & SPI_HALF_DUPLEX) {
		rx_buf[0].buf = rx;
		rx_buf[0].len = rx_length;
		rx_set.count = 1;
	} else {
		/* Talking to a half-duplex device via a full-duplex SPI driver,  */
		/* it's necessary to ignore the data returned during send.  The   */
		/* SPI driver should not copy out the data if the buffer is NULL. */
		rx_buf[0].buf = NULL;
		rx_buf[0].len = tx_length;
		rx_buf[1].buf = rx;
		rx_buf[1].len = rx_length;
	}

	ret = spi_transceive_dt(spi_obj, &tx_set, &rx_set);
	if (ret) {
		LOG_ERR("spi_transceive FAIL %d\n", ret);
		whd_ret = WHD_WLAN_SDIO_ERROR;
	}

#if defined(SPI_DATA_IRQ_SHARED)
	ret = whd_bus_spi_irq_enable(whd_driver, true);
	if (ret) {
		LOG_ERR("whd_bus_spi_irq_enable FAIL %d\n", ret);
		whd_ret = WHD_WLAN_SDIO_ERROR;
	}
#endif

	whd_bus_unlock();
	return whd_ret;
}

/*
 * Implement OOB functionality
 */

void whd_bus_spi_oob_irq_handler(const struct device *port, struct gpio_callback *cb,
				 gpio_port_pins_t pins)
{
#if DT_INST_NODE_HAS_PROP(0, wifi_host_wake_gpios)
	struct airoc_wifi_data *data = CONTAINER_OF(cb, struct airoc_wifi_data, host_oob_pin_cb);

	/* Get OOB pin info */
	const whd_oob_config_t *oob_config = &data->whd_drv->bus_priv->spi_config.oob_config;
	const struct gpio_dt_spec *host_oob_pin = oob_config->host_oob_pin;

	/* Check OOB state is correct */
	int expected_event = (oob_config->is_falling_edge == WHD_TRUE) ? 0 : 1;

	if (!(pins & BIT(host_oob_pin->pin)) || (gpio_pin_get_dt(host_oob_pin) != expected_event)) {
		WPRINT_WHD_ERROR(("Unexpected interrupt event %d\n", expected_event));
		return;
	}

	/* Call thread notify to wake up WHD thread */
	whd_thread_notify_irq(data->whd_drv);

	/* The host-wake is shared — give a BT transport a chance to wake too. */
	whd_bt_notify_irq();

#endif /* DT_INST_NODE_HAS_PROP(0, wifi-host-wake-gpios) */
}

whd_result_t whd_bus_spi_irq_register(whd_driver_t whd_driver)
{
#if DT_INST_NODE_HAS_PROP(0, wifi_host_wake_gpios)
	int ret;
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	struct airoc_wifi_data *data = dev->data;

	/* Get OOB pin info */
	const whd_oob_config_t *oob_config = &whd_driver->bus_priv->spi_config.oob_config;
	const struct gpio_dt_spec *host_oob_pin = oob_config->host_oob_pin;

	/* Check if OOB pin is ready */
	if (!gpio_is_ready_dt(host_oob_pin)) {
		WPRINT_WHD_ERROR(("%s: Failed at gpio_is_ready_dt for host_oob_pin\n", __func__));
		return WHD_HAL_ERROR;
	}

	/* Configure OOB pin as input */
	ret = gpio_pin_configure_dt(host_oob_pin, GPIO_INPUT);
	if (ret != 0) {
		WPRINT_WHD_ERROR((
			" %s: Failed at gpio_pin_configure_dt for host_oob_pin, result code = %d\n",
			__func__, ret));
		return WHD_HAL_ERROR;
	}

	/* Initialize/add OOB pin callback */
	gpio_init_callback(&data->host_oob_pin_cb, whd_bus_spi_oob_irq_handler,
			   BIT(host_oob_pin->pin));

	ret = gpio_add_callback_dt(host_oob_pin, &data->host_oob_pin_cb);
	if (ret != 0) {
		WPRINT_WHD_ERROR(
			("%s: Failed at gpio_add_callback_dt for host_oob_pin, result code = %d\n",
			 __func__, ret));
		return WHD_HAL_ERROR;
	}
#endif /* DT_INST_NODE_HAS_PROP(0, wifi_host_wake_gpios) */

	return WHD_SUCCESS;
}

whd_result_t whd_bus_spi_irq_enable(whd_driver_t whd_driver, whd_bool_t enable)
{
#if DT_INST_NODE_HAS_PROP(0, wifi_host_wake_gpios)
	int ret;
	const whd_oob_config_t *oob_config = &whd_driver->bus_priv->spi_config.oob_config;
	struct gpio_dt_spec *gpio = (struct gpio_dt_spec *)oob_config->host_oob_pin;

#if defined(SPI_DATA_IRQ_SHARED)
	const struct device *dev = DEVICE_DT_INST_GET(0);
	const struct airoc_wifi_config *config = dev->config;
	struct airoc_wifi_data *data = dev->data;

	if (enable) {
		ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_HOST_WAKE);
		__ASSERT(ret == 0, "could not apply pinconfig, return code %d", ret);
		ret = gpio_pin_interrupt_configure_dt(
			gpio, (oob_config->is_falling_edge == WHD_TRUE) ? GPIO_INT_EDGE_FALLING
									: GPIO_INT_EDGE_RISING);
		__ASSERT(ret == 0, "gpio_pin_interrupt_configure_dt failed, return code %d", ret);

		int state = gpio_pin_get_dt(gpio);
		int expected_event = (oob_config->is_falling_edge == WHD_TRUE) ? 0 : 1;

		/* Notify only if interrupt wasn't assert before. This code assumes that if */
		/* the interrupt was previously asserted, then the thread has already been  */
		/* notified.                                                                */
		if (state == expected_event && state != data->prev_irq_state) {
			whd_thread_notify_irq(whd_driver);
		}
	} else {
		data->prev_irq_state = gpio_pin_get_dt(gpio);
		ret = gpio_pin_interrupt_configure_dt(gpio, GPIO_INT_DISABLE);
		__ASSERT(ret == 0, "gpio_pin_interrupt_configure_dt failed, return code %d", ret);
		pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	}
#else
	if (enable) {
		ret = gpio_pin_interrupt_configure_dt(
			gpio, (oob_config->is_falling_edge == WHD_TRUE) ? GPIO_INT_EDGE_FALLING
									: GPIO_INT_EDGE_RISING);
		__ASSERT(ret == 0, "gpio_pin_interrupt_configure_dt failed, return code %d", ret);
	} else {
		ret = gpio_pin_interrupt_configure_dt(gpio, GPIO_INT_DISABLE);
		__ASSERT(ret == 0, "gpio_pin_interrupt_configure_dt failed, return code %d", ret);
	}
#endif /* defined(SPI_DATA_IRQ_SHARED) */

#endif /* DT_INST_NODE_HAS_PROP(0, wifi_host_wake_gpios) */

	return WHD_SUCCESS;
}

#ifdef __cplusplus
} /* extern "C" */
#endif
