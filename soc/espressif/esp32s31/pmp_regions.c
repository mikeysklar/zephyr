/*
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/riscv/csr.h>
#include <zephyr/devicetree.h>
#include <pmp.h>

/*
 * ESP32-S31 SoC ROM region.
 *
 * The ROM (hal_espressif soc.h: SOC_IROM_MASK_LOW/HIGH, 0x2F800000-
 * 0x2F850000) holds libc and other utility functions and must stay
 * readable and executable from both kernel and user mode.
 */
#define SOC_ROM_NODE DT_NODELABEL(soc_rom)

PMP_SOC_REGION_DEFINE(esp32s31_soc_rom, DT_REG_ADDR(SOC_ROM_NODE),
		      DT_REG_ADDR(SOC_ROM_NODE) + DT_REG_SIZE(SOC_ROM_NODE), PMP_R | PMP_X);

/*
 * IRAM and DRAM share the same 512KB physical SRAM
 * (hal_espressif soc.h: SOC_IRAM_LOW/HIGH == SOC_DRAM_LOW/HIGH,
 * 0x2F000000-0x2F080000) and the code/data split is decided at link
 * time, so only the linked IRAM text range may be executable.
 */
extern char _iram_text_start[];
extern char _iram_text_end[];

PMP_SOC_REGION_DEFINE(esp32s31_iram_text, _iram_text_start, _iram_text_end, PMP_R | PMP_X);

/*
 * Flash-mapped read-only data (DROM). Const data and string
 * literals live in a separate MMU window from executable flash
 * text (__rom_region). Without an explicit PMP entry, user mode
 * cannot read that window. Use _image_rodata_* so sections after
 * __rodata_region_end that still map into DROM are covered.
 */
extern char _image_rodata_start[];
extern char _image_rodata_end[];

PMP_SOC_REGION_DEFINE(esp32s31_flash_rodata, _image_rodata_start, _image_rodata_end, PMP_R);

/*
 * TODO: peripheral bus PMP region intentionally not defined yet.
 *
 * Unlike ESP32-C61 (single 0x60000000-0x60100000 window) or ESP32-P4
 * (0x50000000-0x50200000), hal_espressif's esp32s31 reg_base.h places
 * peripherals across several disjoint sub-ranges rather than one
 * contiguous block -- computed from the real base addresses in that
 * header, they span roughly:
 *   - 0x20103000-0x2039E000  IEEE 802.15.4 + HP peripherals (UART, SPI,
 *                             I2C, timers, USB-OTG-HS, GMAC, ...)
 *   - 0x20500000-0x20511000  flash/crypto/access-control subsystem
 *   - 0x20580000-0x2058B000  HP sysreg/GPIO/clkrst subsystem
 *   - 0x20700000-0x2081B000  LP subsystem (LP peripherals, PMU, efuse,
 *                             RTC timer/WDT, ADC, touch, ...)
 *   - 0x2C000000-0x2C000xxx  cache config
 *   - 0x2D000000-0x2D005000  trace / bus / memory monitor
 * hal_espressif's own soc.h marks SOC_PERIPHERAL_LOW/HIGH as
 * "TODO need update" for this chip (still preview silicon as of
 * ESP-IDF v6.1), and a single window wide enough to cover all of the
 * above would also swallow HP-SRAM (0x2F000000-0x2F080000) and LP-RAM
 * (0x2E000000-0x2E008000), which must not get a blanket peripheral
 * R|W PMP entry layered over the more specific IRAM text (R|X) region
 * above -- PMP region evaluation order matters here.
 *
 * Once hal_espressif publishes a firm peripheral bus boundary (or this
 * is confirmed empirically per sub-range against real hardware), add
 * one PMP_SOC_REGION_DEFINE() per disjoint sub-range above instead of
 * a single guessed window.
 */
