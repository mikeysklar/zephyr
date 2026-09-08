/*
 * Copyright (c) 2026 Adafruit Industries
 * SPDX-License-Identifier: Apache-2.0
 *
 * Runs pico-sdk's cybt_shared_bus BT transport over the upstream Infineon WHD
 * WiFi bus, so the CYW43439's Bluetooth controller is reachable on boards
 * (Pico W, Pico 2 W) where HCI shares the WiFi gSPI bus instead of sitting on
 * a UART.
 *
 * cybt was written against the georgerobotics cyw43_ll stack, but its coupling
 * to it is a small seam: four backplane primitives, a bus lock, a delay, and a
 * malloc/free. This file supplies all of them, backed by WHD:
 *
 *   cyw43_ll_{read,write}_backplane_reg -> whd_bus_{read,write}_backplane_value
 *   cyw43_ll_{read,write}_backplane_mem -> whd_bus_transfer_backplane_bytes
 *   cyw43_thread_enter/exit             -> the shared recursive gSPI bus lock
 *   cyw43_delay_ms/us                   -> k_msleep / k_busy_wait
 *
 * It also implements the transport primitives the HCI driver calls
 * (cyw43_bluetooth_hci_init/read/write) and runs the controller->host RX poll
 * thread, which in a georgerobotics build would belong to that stack's own
 * single poll thread.
 *
 * Derived from the WHD port in beriberikix/zephyr-cyw43-driver (Apache-2.0),
 * proposed upstream as zephyrproject-rtos/zephyr#111811.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

/* georgerobotics types plus the cybt entry points, from the hal_rpi_pico
 * lib/cyw43-driver submodule. */
#include "cyw43_ll.h"
#include "cyw43_btbus.h"
#include "cybt_shared_bus_driver.h"

/* WHD / AIROC */
#include "whd.h"
#include "whd_int.h"
#include "whd_wifi_api.h"
#include "whd_bus_protocol_interface.h"
#include "whd_bus_common.h"
#include "airoc_wifi.h"

#include "cyw43_bt_whd_glue.h"

LOG_MODULE_REGISTER(cyw43_bt_glue, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

struct cyw43_bt_state cyw43_bt_state;

/*
 * One recursive mutex serializing every gSPI transfer — WHD's WLAN thread (via
 * the bracket patched into whd_bus_spi_transfer) and the BT path. WHD itself
 * has no bus mutex; it relies on "only whd_thread touches the bus", which the
 * BT path violates. Without this, concurrent WiFi+BT traffic corrupts transfers
 * below the host lock: WiFi association fails and the BT ring indices read back
 * out of range. k_mutex is recursive per owner and grants priority inheritance,
 * matching the single-bus lock cybt was written against.
 */
static K_MUTEX_DEFINE(cyw43_bus_mutex);

void whd_bus_lock(void)
{
	k_mutex_lock(&cyw43_bus_mutex, K_FOREVER);
}

void whd_bus_unlock(void)
{
	k_mutex_unlock(&cyw43_bus_mutex);
}

/* The names cybt's CYW43_THREAD_ENTER/EXIT expand to. */
void cyw43_thread_enter(void)
{
	whd_bus_lock();
}

void cyw43_thread_exit(void)
{
	whd_bus_unlock();
}

void cyw43_delay_ms(uint32_t ms)
{
	k_msleep((int32_t)ms);
}

void cyw43_delay_us(uint32_t us)
{
	k_busy_wait(us);
}

/* ------------------------------------------------------------------ handle */

static inline whd_driver_t bt_whd_driver(void)
{
	whd_interface_t ifp = airoc_wifi_get_whd_interface();

	return ifp ? ifp->whd_driver : NULL;
}

/* ------------------------------------------------ backplane shims over WHD */

void cyw43_ll_write_backplane_reg(cyw43_ll_t *self_in, uint32_t addr, uint32_t val)
{
	ARG_UNUSED(self_in);
	whd_bus_write_backplane_value(bt_whd_driver(), addr, 4, val);
}

uint32_t cyw43_ll_read_backplane_reg(cyw43_ll_t *self_in, uint32_t addr)
{
	uint8_t b[4] = {0};

	ARG_UNUSED(self_in);
	whd_bus_read_backplane_value(bt_whd_driver(), addr, 4, b);
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
	       ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

int cyw43_ll_write_backplane_mem(cyw43_ll_t *self_in, uint32_t addr, uint32_t len,
				 const uint8_t *buf)
{
	ARG_UNUSED(self_in);
	return (int)whd_bus_transfer_backplane_bytes(bt_whd_driver(), BUS_WRITE, addr, len,
						     (uint8_t *)buf);
}

int cyw43_ll_read_backplane_mem(cyw43_ll_t *self_in, uint32_t addr, uint32_t len, uint8_t *buf)
{
	ARG_UNUSED(self_in);
	return (int)whd_bus_transfer_backplane_bytes(bt_whd_driver(), BUS_READ, addr, len, buf);
}

/* -------------------------------------------------------- BT RX poll thread */

#define BT_POLL_STACK_SIZE 4096

/*
 * The RX poll must out-prioritise WHD's WLAN threads. WHD runs its bus thread
 * and the AIROC event task at CY_RTOS_PRIORITY_HIGH (preemptible priority 4);
 * a poll below them gets preempted by any WiFi activity, which delays BT RX
 * past the connection-event timing and drops the link during GATT discovery.
 * Running one band above both means a BT servicing preempts WiFi RX as soon as
 * the interval elapses. The shared recursive bus lock still lets an in-flight
 * WHD transfer finish via priority inheritance, so WiFi is slowed, not starved.
 */
#define BT_POLL_PRIO        K_PRIO_PREEMPT(2)

/*
 * Fast enough for HCI command/event round-trips and ~10 notifications/s of
 * coexistence traffic, slow enough not to hammer the backplane at idle.
 */
#define BT_POLL_INTERVAL_MS 4

static K_KERNEL_STACK_DEFINE(bt_poll_stack, BT_POLL_STACK_SIZE);
static struct k_thread bt_poll_thread_data;
static bool bt_poll_started;

static void bt_poll_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		whd_bus_lock();
		if (cyw43_bluetooth_has_pending()) {
			cyw43_bluetooth_hci_process();
		}
		whd_bus_unlock();
		k_msleep(BT_POLL_INTERVAL_MS);
	}
}

static void bt_poll_start(void)
{
	if (bt_poll_started) {
		return;
	}
	bt_poll_started = true;
	k_thread_create(&bt_poll_thread_data, bt_poll_stack, BT_POLL_STACK_SIZE,
			bt_poll_thread, NULL, NULL, NULL, BT_POLL_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&bt_poll_thread_data, "cyw43_bt_poll");
}

/* ------------------------------------------------ transport bring-up (lazy) */

/*
 * WHD already powered the chip and brought the backplane up during AIROC init
 * (whd_wifi_on), so bring-up here only has to download the BT patchram, once,
 * on the first BT operation.
 */
static int cyw43_bt_ensure_up(void)
{
	whd_interface_t ifp;
	whd_mac_t mac;
	int ret;

	if (cyw43_bt_state.bt_loaded) {
		return 0;
	}

	ifp = airoc_wifi_get_whd_interface();
	if (ifp == NULL || ifp->whd_driver == NULL) {
		LOG_ERR("WHD is not up yet; cannot bring Bluetooth up");
		return -EIO;
	}

	/* Cache the WiFi MAC so the HCI setup hook can check BD_ADDR == MAC+1. */
	if (whd_wifi_get_mac_address(ifp, &mac) == WHD_SUCCESS) {
		memcpy(cyw43_bt_state.mac, mac.octet, sizeof(cyw43_bt_state.mac));
	} else {
		LOG_WRN("whd_wifi_get_mac_address failed; BD_ADDR check may not match");
	}

	whd_bus_lock();
	ret = cyw43_btbus_init(NULL);
	whd_bus_unlock();
	if (ret) {
		LOG_ERR("cyw43_btbus_init (BT patchram download) failed: %d", ret);
		return ret;
	}

	cyw43_bt_state.bt_loaded = true;
	bt_poll_start();
	LOG_INF("Bluetooth up over WHD bus (patchram loaded), WiFi MAC %02x:%02x:%02x:%02x:%02x:%02x",
		cyw43_bt_state.mac[0], cyw43_bt_state.mac[1], cyw43_bt_state.mac[2],
		cyw43_bt_state.mac[3], cyw43_bt_state.mac[4], cyw43_bt_state.mac[5]);
	return 0;
}

int cyw43_bluetooth_hci_init(void)
{
	return cyw43_bt_ensure_up();
}

int cyw43_bluetooth_hci_read(uint8_t *buf, uint32_t max_size, uint32_t *len)
{
	int ret = cyw43_bt_ensure_up();

	if (ret) {
		return ret;
	}
	return cyw43_btbus_read(buf, max_size, len);
}

int cyw43_bluetooth_hci_write(uint8_t *buf, size_t len)
{
	int ret = cyw43_bt_ensure_up();

	if (ret) {
		return ret;
	}
	return cyw43_btbus_write(buf, (uint32_t)len);
}
