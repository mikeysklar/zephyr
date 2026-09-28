/*
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/linker/linker-defs.h>

#define MCUBOOT_START_ADDR DT_REG_ADDR(DT_NODELABEL(axisram2))

/* Only meaningful when MCUboot loads the image into RAM (CONFIG_BOOT_RAM_LOAD):
 * the loaded image's RAM footprint must stay below the MCUboot slot area at
 * axisram2. XIP images execute from flash and don't consume image-sized RAM,
 * and boards whose zephyr,sram is axisram2 itself (e.g. nucleo_n657x0_q) trip
 * this assert unconditionally, so skip the check for them.
 */
#ifndef CONFIG_XIP
#ifndef CONFIG_BOOT_RAM_LOAD
ASSERT((__kernel_ram_start < (MCUBOOT_START_ADDR - CONFIG_ROM_START_OFFSET)),
       "
       Image is too large to fit RAMLOAD configuration. Consider using XIP.
       ")
#endif
#endif
