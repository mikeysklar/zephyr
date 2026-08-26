/*
 * Copyright (c) 2023 Antmicro
 * Copyright (c) 2024-2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sw_isr_table.h>
#include <zephyr/sys/barrier.h>

#include <em_device.h>
#include "power.h"
#include <sl_si91x_hal_soc_soft_reset.h>


#if defined(CONFIG_MEMC_SILABS_SIWX91X_QSPI)
/* PSRAM data cache, family reference manual rev 1.2 section 5.4.5. It has no
 * devicetree node and no driver, so the offsets are spelled out here.
 */
#define SIWX91X_PSRAM_DCACHE_BASE   0x44040000UL
#define SIWX91X_PSRAM_DCACHE_CTRL   (SIWX91X_PSRAM_DCACHE_BASE + 0x010UL)
#define SIWX91X_PSRAM_DCACHE_ENABLE BIT(0)

/* The bootloader leaves this cache enabled and half configured, and nothing
 * maintains it. WiseConnect clears the HPROT allocate signal right after
 * enabling it, but that path is behind SL_SI91X_D_CACHE_ENABLE and its macros
 * are not in the HAL. Zephyr cannot maintain it either: cache_siwx91x.c
 * asserts the SoC has no data cache, CPU_HAS_DCACHE is never selected, and
 * sys_cache_data_* return -ENOTSUP.
 *
 * That leaves an unmaintained write allocate cache in front of memory the
 * network processor also writes, which corrupts PSRAM contents under load.
 * Disable it before anything can map or use PSRAM.
 *
 * Only the enable bit clears. Bit 1 is sticky and the maintenance status
 * register settles at 0x100 rather than 0, so do not poll it for zero.
 */
static void siwx91x_disable_psram_dcache(void)
{
	volatile uint32_t *ctrl = (volatile uint32_t *)SIWX91X_PSRAM_DCACHE_CTRL;

	*ctrl &= ~SIWX91X_PSRAM_DCACHE_ENABLE;
	barrier_dsync_fence_full();
	barrier_isync_fence_full();
}
#endif

void soc_early_init_hook(void)
{
	SystemInit();

#if defined(CONFIG_MEMC_SILABS_SIWX91X_QSPI)
	siwx91x_disable_psram_dcache();
#endif

	if (IS_ENABLED(CONFIG_PM)) {
		siwx91x_power_init();
	}
}

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	sl_si91x_soc_nvic_reset();
}

/* SiWx917's bootloader requires IRQn 32 to hold payload's entry point address. */
extern void z_arm_reset(void);
Z_ISR_DECLARE_DIRECT(32, 0, z_arm_reset);
