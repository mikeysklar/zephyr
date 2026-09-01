/*
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

/* HP-SRAM (512kB) memory, unified I/D address space.
 * From hal_espressif soc.h: SOC_IRAM_LOW = 0x2F000000, SOC_IRAM_HIGH = 0x2F080000
 * (SOC_DRAM_LOW/HIGH are aliases of the same range on this chip).
 */
#define HPSRAM_START      DT_REG_ADDR(DT_NODELABEL(sramhp))
#define HPSRAM_SIZE       DT_REG_SIZE(DT_NODELABEL(sramhp))
#define HPSRAM_DRAM_START HPSRAM_START
#define HPSRAM_IRAM_START HPSRAM_START

/* ROM stack location, straight from hal_espressif soc.h
 * (SOC_ROM_STACK_START / SOC_ROM_STACK_SIZE): only relevant during early
 * boot, before the 2nd stage bootloader or application takes over the top
 * of SRAM.
 */
#define ROM_STACK_START (0x2f07cfb0)
#define ROM_STACK_SIZE  0x2000

/*
 * TODO: the exact split of the region below ROM_STACK_START between
 * ROM shared download-mode buffers (reclaimable as heap once the app is
 * running) and ROM .bss/.data (not reclaimable) is not yet known for
 * ESP32-S31 -- hal_espressif's soc.h only publishes the stack location.
 * ESP32-C61/H4 pin this from the ROM ELF/map file (esp-rom-elfs). Until
 * an ESP32-S31 ROM map is available, the second stage bootloader should
 * treat everything below ROM_STACK_START, minus BOOTLOADER_STACK_OVERHEAD,
 * as available and MUST NOT assume shared buffers are reclaimable early.
 */
#define DRAM_STACK_START ROM_STACK_START
#define DRAM_USER_END    DRAM_STACK_START

/* Stack headroom kept free above the loader segments */
#define BOOTLOADER_STACK_OVERHEAD 0x2000

#define BOOTLOADER_IRAM_LOADER_SEG_LEN 0x1C00
#define BOOTLOADER_DRAM_LOADER_SEG_LEN 0x0C00

#define BOOTLOADER_USER_DRAM_END (DRAM_USER_END - BOOTLOADER_STACK_OVERHEAD)

#define BOOTLOADER_IRAM_LOADER_SEG_START (BOOTLOADER_USER_DRAM_END - BOOTLOADER_IRAM_LOADER_SEG_LEN)
#define BOOTLOADER_DRAM_LOADER_SEG_START                                                           \
	(BOOTLOADER_IRAM_LOADER_SEG_START - BOOTLOADER_DRAM_LOADER_SEG_LEN)

/* MCUboot iram/dram segments: placed in upper half of SRAM, below iram_loader_seg.
 * The lower half is reserved for the application image.
 */
#define BOOTLOADER_IRAM_SEG_TARGET_LEN ((BOOTLOADER_DRAM_LOADER_SEG_START - HPSRAM_START) / 4)
#define BOOTLOADER_IRAM_SEG_START                                                                  \
	ALIGN_UP(BOOTLOADER_DRAM_LOADER_SEG_START - BOOTLOADER_IRAM_SEG_TARGET_LEN, 0x100)
#define BOOTLOADER_IRAM_SEG_LEN   (BOOTLOADER_DRAM_LOADER_SEG_START - BOOTLOADER_IRAM_SEG_START)
#define BOOTLOADER_DRAM_SEG_LEN   BOOTLOADER_IRAM_SEG_LEN
#define BOOTLOADER_DRAM_SEG_START (BOOTLOADER_IRAM_SEG_START - BOOTLOADER_DRAM_SEG_LEN)

/* Flash */
#define FLASH_SIZE         DT_REG_SIZE(DT_CHOSEN(zephyr_flash))
#define FLASH_BASE_ADDRESS DT_REG_ADDR(DT_CHOSEN(zephyr_flash))

/* Cached memory. From hal_espressif soc.h: SOC_IROM_LOW = SOC_DROM_LOW =
 * 0x40000000, i.e. flash and PSRAM share one 64MB cache-mapped virtual
 * window like other unified-cache Espressif RISC-V chips.
 */
#define CACHE_ALIGN  CONFIG_MMU_PAGE_SIZE
#define IROM_SEG_ORG 0x40000000
#define IROM_SEG_LEN FLASH_SIZE
/* DROM shares the unified-cache linear address space with IROM; see the
 * ESP32-C61 default.ld comment for why DROM_SEG_ORG == IROM_SEG_ORG.
 */
#define DROM_SEG_ORG IROM_SEG_ORG
#define DROM_SEG_LEN FLASH_SIZE

/* External RAM (PSRAM) cache window. From hal_espressif soc.h:
 * SOC_EXTRAM_LOW = 0x50000000, SOC_EXTRAM_HIGH = 0x54000000 (64MB).
 * Derived here from the DT ext_ram node so the linker uses the full
 * virtual range the MMU can map, not the physical chip size.
 */
#define EXTRAM_START DT_REG_ADDR(DT_NODELABEL(ext_ram))
#define EXTRAM_SIZE  DT_REG_SIZE(DT_NODELABEL(ext_ram))
