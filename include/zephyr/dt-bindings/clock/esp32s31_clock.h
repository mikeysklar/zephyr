/*
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP32-S31 clock definitions for device tree bindings
 *
 * Values are taken from hal_espressif's esp32s31 soc/clk_tree_defs.h
 * (soc_cpu_clk_src_t, soc_rtc_slow_clk_src_t, soc_rtc_fast_clk_src_t,
 * soc_xtal_freq_t) and soc/periph_defs.h (shared_periph_module_t).
 *
 * ESP32-S31's clock source enums differ from other Espressif RISC-V
 * chips: it has a 4th CPU clock source (PLL_F240M) that e.g. ESP32-C61
 * lacks, and only 2 RTC_SLOW_CLK / 2 RTC_FAST_CLK sources where C61 has
 * more (no OSC_SLOW/EXT_OSC slow source, no XTAL_D2 fast source on
 * ESP32-S31).
 *
 * Two module ID ranges: the shared/modem-clock-managed IDs below come
 * straight from shared_periph_module_t in hal_espressif's esp32s31
 * soc/periph_defs.h. The "non-shared" IDs (>= 100) are esp_hw_support/
 * periph_ctrl.c's own dispatch values, not real PCR/HP_SYS_CLKRST
 * register bit positions -- the actual clock-gate/reset register
 * access for those happens inside each peripheral's own chip-specific
 * xx_ll_enable_bus_clock() / xx_ll_reset_register() functions (e.g.
 * esp_hal_ledc/esp32s31/include/hal/ledc_ll.h), which already exist
 * for ESP32-S31 and were verified present before assigning IDs to
 * them. Only the 5 periph_ctrl.c requires unconditionally (LEDC,
 * UART0, I2C0, GDMA, PCNT) are defined so far; the rest of
 * periph_ctrl.c's #if defined(...)-guarded cases are safely skipped
 * when their module ID isn't defined, so more can be added later
 * without needing all of them now.
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_ESP32S31_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_ESP32S31_H_

/* Supported CPU clock sources */
#define ESP32_CPU_CLK_SRC_XTAL       0U /**< CPU clock source: XTAL */
#define ESP32_CPU_CLK_SRC_CPLL       1U /**< CPU clock source: CPLL (crystal oscillator freq. multiplier, up to 320MHz) */
#define ESP32_CPU_CLK_SRC_RC_FAST    2U /**< CPU clock source: RC fast */
#define ESP32_CPU_CLK_SRC_PLL_F240M  3U /**< CPU clock source: PLL_F240M (derived from 480MHz BBPLL) */

/* Supported RC fast clock frequency */
#define ESP32_CLK_RC_FAST_FREQ 17500000 /**< RC fast clock, approximate, Hz */

/* Supported XTAL frequency */
#define ESP32_CLK_XTAL_40M 40000000 /**< XTAL clock 40 MHz */

/* Supported RTC fast clock sources */
#define ESP32_RTC_FAST_CLK_SRC_RC_FAST 0 /**< RTC fast clock source: RC fast */
#define ESP32_RTC_FAST_CLK_SRC_XTAL    1 /**< RTC fast clock source: XTAL */

/* Supported RTC slow clock sources */
#define ESP32_RTC_SLOW_CLK_SRC_RC_SLOW  0 /**< RTC slow clock source: RC slow */
#define ESP32_RTC_SLOW_CLK_SRC_XTAL32K  1 /**< RTC slow clock source: XTAL 32 KHz */

/* RTC slow clock frequency (XTAL32K path; RC_SLOW's approximate
 * frequency is not yet published in hal_espressif's esp32s31 headers).
 */
#define ESP32_RTC_SLOW_CLK_SRC_XTAL32K_FREQ 32768 /**< XTAL 32 KHz clock frequency */

/* Shared module IDs - must match shared_periph_module_t enum in
 * hal_espressif's esp32s31 soc/periph_defs.h. These are used by the
 * clock control driver to identify peripheral modules. Order and
 * values come straight from that enum; do not renumber to match other
 * Espressif targets' clock.h -- ESP32-S31's shared_periph_module_t
 * order differs (e.g. LCD_CAM is first here, not TIMG0).
 */
#define ESP32_LCD_CAM_MODULE  0 /**< LCD/camera module */
#define ESP32_TIMG0_MODULE    1 /**< Timer group 0 module */
#define ESP32_TIMG1_MODULE    2 /**< Timer group 1 module */
#define ESP32_UHCI0_MODULE    3 /**< UHCI0 module */
#define ESP32_SYSTIMER_MODULE 4 /**< System timer module */
/* Peripherals clock managed by the modem_clock driver must be listed last */
#define ESP32_WIFI_MODULE            5  /**< Wi-Fi module */
#define ESP32_BT_MODULE               6  /**< Bluetooth module */
#define ESP32_IEEE802154_MODULE       7  /**< IEEE 802.15.4 module */
#define ESP32_COEX_MODULE             8  /**< Wi-Fi/BT coexistence module */
#define ESP32_PHY_MODULE               9  /**< PHY module */
#define ESP32_ANA_I2C_MASTER_MODULE    10 /**< Analog I2C master module */
#define ESP32_MODEM_ETM_MODULE         11 /**< Modem ETM module */
#define ESP32_MODEM_ADC_COMMON_FE_MODULE 12 /**< Modem ADC common front-end module */
#define ESP32_PHY_CALIBRATION_MODULE   13 /**< PHY calibration module */
#define ESP32_MODULE_MAX               14 /**< Number of shared modules */

/* Non-shared peripherals - these have dedicated clock control in their
 * drivers and don't use periph_module_enable(). Values start at 100,
 * matching the convention every other Espressif target's clock.h uses
 * (these are locally scoped per-chip header defines, not a global
 * registry, so numeric collisions across chips are not possible --
 * matching values are purely for readability).
 */
#define ESP32_LEDC_MODULE  100 /**< LEDC module */
#define ESP32_UART0_MODULE 101 /**< UART0 module */
#define ESP32_I2C0_MODULE  105 /**< I2C0 module */
#define ESP32_GDMA_MODULE  110 /**< GDMA module */
#define ESP32_PCNT_MODULE  107 /**< PCNT module */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_ESP32S31_H_ */
