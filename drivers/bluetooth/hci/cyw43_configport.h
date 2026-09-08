/*
 * Copyright (c) 2026 Adafruit Industries
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal cyw43-driver config port for the CYW43 shared-bus BT HCI transport.
 *
 * The georgerobotics cyw43-driver headers (cyw43_ll.h, cyw43_btbus.h) and
 * pico-sdk's cybt_shared_bus include cyw43_config.h, which pulls in this file.
 * Unlike pico-sdk's config port, this one configures ONLY what the Bluetooth
 * shared-bus transport needs: WiFi is driven by the upstream Zephyr AIROC/WHD
 * driver, so none of the georgerobotics WiFi/SPI/lwIP machinery is compiled.
 *
 * The four cyw43_ll_*_backplane_* accessors that cybt calls are implemented in
 * cyw43_bt_whd_glue.c on top of WHD's bus, and CYW43_THREAD_ENTER/EXIT map to
 * the shared gSPI bus lock that WHD's WLAN path also takes.
 */

#ifndef ZEPHYR_DRIVERS_BLUETOOTH_HCI_CYW43_CONFIGPORT_H_
#define ZEPHYR_DRIVERS_BLUETOOTH_HCI_CYW43_CONFIGPORT_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
/* cybt_shared_bus.c uses assert() without including this itself. */
#include <assert.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No lwIP and no georgerobotics WiFi: BT transport only. */
#define CYW43_LWIP              (0)
#define CYW43_NO_NETUTILS       (1)
#define CYW43_USE_STATS         (0)
#define CYW43_GPIO              (0)
#define CYW43_ENABLE_BLUETOOTH  (1)

/* The 43439 patchram ships as a binary blob, not Broadcom hex records. */
#define CYW43_USE_HEX_BTFW      (0)

/* Largest backplane block cybt will move in one transfer. */
#ifndef CYW43_BUS_MAX_BLOCK_SIZE
#define CYW43_BUS_MAX_BLOCK_SIZE (64u)
#endif

/* Firmware arrays are plain const data here (no custom linker placement). */
#ifndef CYW43_RESOURCE_ATTRIBUTE
#define CYW43_RESOURCE_ATTRIBUTE
#endif

/*
 * Bus serialization. cybt brackets its multi-transfer read-modify-write
 * sequences with these; they resolve to the recursive mutex in
 * cyw43_bt_whd_glue.c that the patched WHD SPI HAL also takes, so BT and the
 * WLAN thread cannot interleave on the single gSPI bus.
 */
void cyw43_thread_enter(void);
void cyw43_thread_exit(void);

#define CYW43_THREAD_ENTER      cyw43_thread_enter();
#define CYW43_THREAD_EXIT       cyw43_thread_exit();
#define CYW43_THREAD_LOCK_CHECK

/* Host delay, backed by the kernel. */
void cyw43_delay_ms(uint32_t ms);
void cyw43_delay_us(uint32_t us);

#ifndef cyw43_malloc
#define cyw43_malloc            malloc
#endif
#ifndef cyw43_free
#define cyw43_free              free
#endif

/*
 * Diagnostics. printk() rather than the logging macros, because these expand
 * inside cybt's own translation units, which register no log module.
 */
#include <zephyr/sys/printk.h>
#ifndef CYW43_PRINTF
#define CYW43_PRINTF(...)       printk(__VA_ARGS__)
#endif

/*
 * cybt's corruption diagnostics call pico-sdk's panic(). Route it to the kernel
 * rather than pulling in the pico runtime, so the diagnostic survives and the
 * failure mode matches the rest of Zephyr.
 */
#ifndef panic
#define panic(...)                                                                                 \
	do {                                                                                       \
		printk("cyw43 BT: " __VA_ARGS__);                                                  \
		printk("\n");                                                                      \
		k_panic();                                                                         \
	} while (0)
#endif

#define CYW43_EPERM             (-EPERM)
#define CYW43_EIO               (-EIO)
#define CYW43_EINVAL            (-EINVAL)
#define CYW43_ETIMEDOUT         (-ETIMEDOUT)

#ifndef static_assert
#define static_assert(expr, ...) BUILD_ASSERT((expr), "" __VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_BLUETOOTH_HCI_CYW43_CONFIGPORT_H_ */
