/*
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hw_init.h>
#include <stdint.h>
#include <esp_cpu.h>
#include <soc/rtc.h>
#include <esp_rom_sys.h>

#include <hal/cache_hal.h>
#include <hal/mmu_hal.h>
#include <hal/mmu_ll.h>
#include <hal/apm_ll.h>

/* Unlike ESP32-C61 (which has soc/pcr_reg.h), ESP32-S31 names this
 * register block HP_SYS_CLKRST instead of PCR. Nothing in this file
 * references PCR_* macros directly (peripheral clock/reset is handled
 * through soc_hw_init()/bootloader_clock_configure() instead), so this
 * include was unused and is dropped rather than aliased.
 */

#include <bootloader_clock.h>
#include <bootloader_flash.h>
#include <esp_flash_internal.h>
#include <esp_log.h>

#include <console_init.h>
#include <flash_init.h>
#include <soc_flash_init.h>
#include <soc_init.h>

const static char *TAG = "hw_init";

int hardware_init(void)
{
	int err = 0;

	soc_hw_init();
	ana_reset_config();
	super_wdt_auto_feed();

	/* The APM access-path filters default to enabled and only allow
	 * masters in TEE mode. Disable them so the application (which runs in
	 * REE mode) is not denied access to peripherals, including the modem
	 * and RF register bus used by the Wi-Fi controller.
	 */
	apm_ll_hp_apm_enable_ctrl_filter_all(false);
	apm_ll_lp_apm_enable_ctrl_filter_all(false);
	/* The CPU APM filter stays at its defaults; it must also be
	 * opened when user mode support is added, since it silently
	 * denies U-mode fetches.
	 */

	/*
	 * esp_cpu_configure_invalid_regions() (PMA setup, needed because
	 * Zephyr's PMP init (z_riscv_pmp_init) enables MPRV which changes
	 * how PMA+PMP interact -- without it, peripheral register access
	 * may fault) was called here, but no implementation exists
	 * anywhere for this chip: hal_espressif's esp32s31/
	 * cpu_region_protect.c only has esp_cpu_configure_region_protection
	 * (a different, PMP-specific function) and is explicitly marked
	 * "TODO: [ESP32S31] IDF-15238" -- confirmed genuinely unfinished in
	 * real esp-idf v6.1-beta1, not just missing from this port. Left
	 * out rather than fabricate PMP/PMA region setup with no reference
	 * to verify against. If peripheral register access faults during
	 * boot, this is the first place to look.
	 */

	bootloader_clock_configure();

#ifdef CONFIG_ESP_CONSOLE
	esp_console_init();
	print_banner();
#endif

	cache_hal_config_t cache_config = {
		.core_nums = 1,
	};
	cache_hal_init(&cache_config);

	mmu_hal_config_t mmu_config = {
		.core_nums = 1,
		.mmu_page_size = CONFIG_MMU_PAGE_SIZE,
	};
	/* Initialize MMU context and page size but skip mmu_hal_unmap_all().
	 * In simple boot mode, ROM bootloader has already set up MMU mappings
	 * for flash access. Calling mmu_hal_unmap_all() would invalidate those
	 * mappings and break access to flash-based code and data (IROM/DROM).
	 */
	mmu_hal_ctx_init(&mmu_config);
	mmu_ll_set_page_size(0, CONFIG_MMU_PAGE_SIZE);

	flash_update_id();

	err = bootloader_flash_xmc_startup();
	if (err != 0) {
		ESP_EARLY_LOGE(TAG, "failed when running XMC startup flow, reboot!");
		return err;
	}

	err = read_bootloader_header();
	if (err != 0) {
		return err;
	}

	err = check_bootloader_validity();
	if (err != 0) {
		return err;
	}

	err = init_spi_flash();
	if (err != 0) {
		return err;
	}

	check_wdt_reset();
	config_wdt();

	return 0;
}
