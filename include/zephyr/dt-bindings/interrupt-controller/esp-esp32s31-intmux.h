/*
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP32-S31 interrupt multiplexer definitions for device tree bindings
 * @ingroup dt_esp32s31_intmux
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_ESP32S31_INTMUX_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_ESP32S31_INTMUX_H_

/**
 * @defgroup dt_esp32s31_intmux Espressif ESP32-S31 interrupt allocator
 * @brief Devicetree interrupt source numbers for the Espressif ESP32-S31.
 * @ingroup devicetree-interrupt_controller
 *
 * Interrupt source numbers for the Espressif ESP32-S31 interrupt allocator, used with the
 * <tt>espressif,esp32-intc</tt> compatible interrupt controller. An interrupt is described by three
 * cells: the interrupt source, the priority and a flags cell. Source numbers follow the pattern
 * @c \<SIGNAL\>_INTR_SOURCE; @ref IRQ_DEFAULT_PRIORITY selects the default priority.
 *
 * Mechanically generated from hal_espressif's esp32s31 soc/interrupts.h
 * periph_interrupt_t enum: each ETS_<NAME> member becomes <NAME>, in the
 * same order (the value IS the enum position -- this is HW-fixed, matching
 * what the CLIC interrupt-matrix router expects).
 *
 * @code{.dts}
 * &uart0 {
 *         interrupts = <UART0_INTR_SOURCE IRQ_DEFAULT_PRIORITY 0>;
 * };
 * @endcode
 * @{
 */

/** @cond INTERNAL_HIDDEN */

#define SYS_ICM_INTR_SOURCE                   0
#define AXI_PERF_MON_INTR_SOURCE              1
#define USB_SERIAL_JTAG_INTR_SOURCE           2
#define SDIO_HOST_INTR_SOURCE                 3
#define SPI2_INTR_SOURCE                      4
#define SPI3_INTR_SOURCE                      5
#define I2S0_INTR_SOURCE                      6
#define I2S1_INTR_SOURCE                      7
#define UHCI0_INTR_SOURCE                     8
#define UART0_INTR_SOURCE                     9
#define UART1_INTR_SOURCE                     10
#define UART2_INTR_SOURCE                     11
#define UART3_INTR_SOURCE                     12
#define LCD_CAM_INTR_SOURCE                   13
#define PWM0_INTR_SOURCE                      14
#define PWM1_INTR_SOURCE                      15
#define PWM2_INTR_SOURCE                      16
#define PWM3_INTR_SOURCE                      17
#define TWAI0_INTR_SOURCE                     18
#define TWAI0_TIMER_INTR_SOURCE               19
#define TWAI1_INTR_SOURCE                     20
#define TWAI1_TIMER_INTR_SOURCE               21
#define RMT_INTR_SOURCE                       22
#define I2C0_INTR_SOURCE                      23
#define I2C1_INTR_SOURCE                      24
#define TIMERGRP0_T0_INTR_SOURCE              25
#define TIMERGRP0_T1_INTR_SOURCE              26
#define TIMERGRP0_WDT_INTR_SOURCE             27
#define TIMERGRP1_T0_INTR_SOURCE              28
#define TIMERGRP1_T1_INTR_SOURCE              29
#define TIMERGRP1_WDT_INTR_SOURCE             30
#define LEDC0_INTR_SOURCE                     31
#define LEDC1_INTR_SOURCE                     32
#define SYSTIMER_TARGET0_INTR_SOURCE          33
#define SYSTIMER_TARGET1_INTR_SOURCE          34
#define SYSTIMER_TARGET2_INTR_SOURCE          35
#define AHB_PDMA_IN_CH0_INTR_SOURCE           36
#define AHB_PDMA_IN_CH1_INTR_SOURCE           37
#define AHB_PDMA_IN_CH2_INTR_SOURCE           38
#define AHB_PDMA_IN_CH3_INTR_SOURCE           39
#define AHB_PDMA_IN_CH4_INTR_SOURCE           40
#define AHB_PDMA_OUT_CH0_INTR_SOURCE          41
#define AHB_PDMA_OUT_CH1_INTR_SOURCE          42
#define AHB_PDMA_OUT_CH2_INTR_SOURCE          43
#define AHB_PDMA_OUT_CH3_INTR_SOURCE          44
#define AHB_PDMA_OUT_CH4_INTR_SOURCE          45
#define ASRC_CHNL0_INTR_SOURCE                46
#define ASRC_CHNL1_INTR_SOURCE                47
#define AXI_PDMA_IN_CH0_INTR_SOURCE           48
#define AXI_PDMA_IN_CH1_INTR_SOURCE           49
#define AXI_PDMA_IN_CH2_INTR_SOURCE           50
#define AXI_PDMA_OUT_CH0_INTR_SOURCE          51
#define AXI_PDMA_OUT_CH1_INTR_SOURCE          52
#define AXI_PDMA_OUT_CH2_INTR_SOURCE          53
#define RSA_INTR_SOURCE                       54
#define AES_INTR_SOURCE                       55
#define SHA_INTR_SOURCE                       56
#define ECC_INTR_SOURCE                       57
#define ECDSA_INTR_SOURCE                     58
#define KM_INTR_SOURCE                        59
#define RMA_INTR_SOURCE                       60
#define GPIO_INTR0_SOURCE                     61
#define GPIO_INTR1_SOURCE                     62
#define GPIO_INTR2_SOURCE                     63
#define GPIO_INTR3_SOURCE                     64
#define CPU_INTR_FROM_CPU_0_SOURCE            65
#define CPU_INTR_FROM_CPU_1_SOURCE            66
#define CPU_INTR_FROM_CPU_2_SOURCE            67
#define CPU_INTR_FROM_CPU_3_SOURCE            68
#define CACHE_INTR_SOURCE                     69
#define CPU_APM_M0_INTR_SOURCE                70
#define CPU_APM_M1_INTR_SOURCE                71
#define CPU_APM_M2_INTR_SOURCE                72
#define CPU_APM_M3_INTR_SOURCE                73
#define HP_MEM_APM_M0_INTR_SOURCE             74
#define HP_MEM_APM_M1_INTR_SOURCE             75
#define HP_MEM_APM_M2_INTR_SOURCE             76
#define HP_MEM_APM_M3_INTR_SOURCE             77
#define HP_MEM_APM_M4_INTR_SOURCE             78
#define HP_MEM_APM_M5_INTR_SOURCE             79
#define CPU_PERI0_TIMEOUT_INTR_SOURCE         80
#define CPU_PERI1_TIMEOUT_INTR_SOURCE         81
#define HP_PERI0_TIMEOUT_INTR_SOURCE          82
#define HP_PERI1_TIMEOUT_INTR_SOURCE          83
#define HP_APM_M0_INTR_SOURCE                 84
#define HP_APM_M1_INTR_SOURCE                 85
#define HP_APM_M2_INTR_SOURCE                 86
#define HP_APM_M3_INTR_SOURCE                 87
#define HP_APM_M4_INTR_SOURCE                 88
#define HP_APM_M5_INTR_SOURCE                 89
#define HP_APM_M6_INTR_SOURCE                 90
#define HP_PERI0_PMS_INTR_SOURCE              91
#define HP_PERI1_PMS_INTR_SOURCE              92
#define CPU0_PERI_PMS_INTR_SOURCE             93
#define CPU1_PERI_PMS_INTR_SOURCE             94
#define MSPI_FLASH_INTR_SOURCE                95
#define LPI_INTR_SOURCE                       96
#define PMT_INTR_SOURCE                       97
#define SBD_INTR_SOURCE                       98
#define USB_OTGHS_INTR_SOURCE                 99
#define USB_OTGHS_ENDP_MULTI_PROC_INTR_SOURCE 100
#define JPEG_INTR_SOURCE                      101
#define PPA_INTR_SOURCE                       102
#define CORE0_TRACE_INTR_SOURCE               103
#define CORE1_TRACE_INTR_SOURCE               104
#define DMA2D_IN_CH0_INTR_SOURCE              105
#define DMA2D_IN_CH1_INTR_SOURCE              106
#define DMA2D_IN_CH2_INTR_SOURCE              107
#define DMA2D_OUT_CH0_INTR_SOURCE             108
#define DMA2D_OUT_CH1_INTR_SOURCE             109
#define DMA2D_OUT_CH2_INTR_SOURCE             110
#define DMA2D_OUT_CH3_INTR_SOURCE             111
#define MSPI_PSRAM_INTR_SOURCE                112
#define HP_SYSREG_INTR_SOURCE                 113
#define PCNT0_INTR_SOURCE                     114
#define PCNT1_INTR_SOURCE                     115
#define HP_PAU_INTR_SOURCE                    116
#define HP_PARLIO_RX_INTR_SOURCE              117
#define HP_PARLIO_TX_INTR_SOURCE              118
#define ASSIST_DEBUG_INTR_SOURCE              119
#define MODEM_WIFI_MAC_INTR_SOURCE            120
#define MODEM_WIFI_MAC_NMI_INTR_SOURCE        121
#define MODEM_WIFI_PWR_INTR_SOURCE            122
#define MODEM_WIFI_BB_INTR_SOURCE             123
#define MODEM_BT_MAC_INTR_SOURCE              124
#define MODEM_BT_BB_INTR_SOURCE               125
#define MODEM_BT_BB_NMI_INTR_SOURCE           126
#define MODEM_LP_TIMER_INTR_SOURCE            127
#define MODEM_COEX_INTR_SOURCE                128
#define MODEM_BLE_TIMER_INTR_SOURCE           129
#define MODEM_BLE_SEC_INTR_SOURCE             130
#define MODEM_I2C_MST_INTR_SOURCE             131
#define MODEM_ZB_MAC_INTR_SOURCE              132
#define MODEM_BT_MAC_INT1_INTR_SOURCE         133
#define CORDIC_INTR_SOURCE                    134
#define ZERO_DET_INTR_SOURCE                  135
#define LP_WDT_INTR_SOURCE                    136
#define LP_TIMER_REG_0_INTR_SOURCE            137
#define LP_TIMER_REG_1_INTR_SOURCE            138
#define MB_HP_INTR_SOURCE                     139
#define MB_LP_INTR_SOURCE                     140
#define PMU_REG_0_INTR_SOURCE                 141
#define PMU_REG_1_INTR_SOURCE                 142
#define LP_ANAPERI_INTR_SOURCE                143
#define LP_ADC_INTR_SOURCE                    144
#define LP_DAC_INTR_SOURCE                    145
#define LP_GPIO_INTR_SOURCE                   146
#define LP_I2C_INTR_SOURCE                    147
#define LP_SPI_INTR_SOURCE                    148
#define LP_TOUCH_INTR_SOURCE                  149
#define LP_TSENS_INTR_SOURCE                  150
#define LP_UART_INTR_SOURCE                   151
#define LP_EFUSE_INTR_SOURCE                  152
#define LP_SW_INTR_SOURCE                     153
#define LP_TRNG_INTR_SOURCE                   154
#define LP_SYSREG_INTR_SOURCE                 155
#define LP_APM_M0_INTR_SOURCE                 156
#define LP_APM_M1_INTR_SOURCE                 157
#define LP_APM_M2_INTR_SOURCE                 158
#define LP_APM_M3_INTR_SOURCE                 159
#define LP_PERI0_PMS_INTR_SOURCE              160
#define LP_PERI1_PMS_INTR_SOURCE              161
#define LP_HUK_INTR_SOURCE                    162
#define LP_PERI_TIMEOUT_INTR_SOURCE           163
#define LP_AHB_PDMA_IN_CH0_INTR_SOURCE        164
#define LP_AHB_PDMA_IN_CH1_INTR_SOURCE        165
#define LP_AHB_PDMA_OUT_CH0_INTR_SOURCE       166
#define LP_AHB_PDMA_OUT_CH1_INTR_SOURCE       167
#define LP_SW_INVALID_SLEEP_INTR_SOURCE       168
#define MAX_INTR_SOURCE                       169 /**< Total number of interrupt sources */

/**
 * @brief Default interrupt priority.
 *
 * Zero will allocate low/medium levels of priority (ESP_INTR_FLAG_LOWMED).
 */
#define IRQ_DEFAULT_PRIORITY 0

#define ESP_INTR_FLAG_SHARED (1 << 8) /**< Interrupt can be shared between ISRs */

/** @endcond */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_INTERRUPT_CONTROLLER_ESP32S31_INTMUX_H_ */
