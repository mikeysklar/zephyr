/*
 * Copyright (c) 2026 Adafruit Industries
 * SPDX-License-Identifier: Apache-2.0
 *
 * Interface between the CYW43 shared-bus HCI driver and its WHD-backed glue.
 */

#ifndef ZEPHYR_DRIVERS_BLUETOOTH_HCI_CYW43_BT_WHD_GLUE_H_
#define ZEPHYR_DRIVERS_BLUETOOTH_HCI_CYW43_BT_WHD_GLUE_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Shared gSPI bus lock, taken by BOTH users of the single bus:
 *  - the BT path, via cyw43_thread_enter/exit (cybt's CYW43_THREAD_ENTER)
 *  - WHD's WLAN path, via the bracket added to whd_bus_spi_transfer()
 * Recursive, so the BT path can re-enter when its backplane ops call back down
 * into the SPI transfer.
 */
void whd_bus_lock(void);
void whd_bus_unlock(void);

/*
 * Controller state the HCI driver needs. The CYW43 derives its BD_ADDR from the
 * WiFi MAC + 1, so the glue caches the MAC once WHD is up.
 */
struct cyw43_bt_shared_state {
	uint8_t mac[6];
	bool bt_loaded;
};

extern struct cyw43_bt_shared_state cyw43_bt_state;

/* Shared-bus HCI transport primitives, implemented over WHD's backplane. */
int cyw43_bluetooth_hci_init(void);
int cyw43_bluetooth_hci_read(uint8_t *buf, uint32_t max_size, uint32_t *len);
int cyw43_bluetooth_hci_write(uint8_t *buf, size_t len);

/* Implemented in the HCI driver; driven by the glue's RX poll thread. */
void cyw43_bluetooth_hci_process(void);
bool cyw43_bluetooth_has_pending(void);

#endif /* ZEPHYR_DRIVERS_BLUETOOTH_HCI_CYW43_BT_WHD_GLUE_H_ */
