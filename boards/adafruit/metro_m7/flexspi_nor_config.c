/*
 * Copyright (c) 2026 Adafruit Industries LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Boot ROM configuration for the Winbond W25Q64JV (8 MB) on both Metro M7
 * boards. Ported from CircuitPython's ports/mimxrt10xx/boards/metro_m7_1011
 * flash_config.c.
 */

#include <flexspi_nor_config.h>

#ifdef CONFIG_NXP_IMXRT_BOOT_HEADER
__attribute__((section(".boot_hdr.conf"), used))

const struct flexspi_nor_config_t qspi_flash_config = {
	.mem_config = {
		.tag = FLEXSPI_CFG_BLK_TAG,
		.version = FLEXSPI_CFG_BLK_VERSION,
		.read_sample_clk_src = FLEXSPI_READ_SAMPLE_CLK_LOOPBACK_FROM_SCK_PAD,
		.cs_hold_time = 3u,
		.cs_setup_time = 3u,
		/* Status register bit 0 is BUSY, active high. */
		.busy_offset = 0u,
		.busy_bit_polarity = 0u,
		/* Set QE (status register 2 bit 1) with sequence 4 before
		 * switching to quad reads.
		 */
		.device_mode_cfg_enable = 1u,
		.device_mode_type = DEVICE_CONFIG_CMD_TYPE_QUAD_ENABLE,
		.device_mode_seq = {
			.seq_num = 1u,
			.seq_id = 4u,
		},
		.device_mode_arg = 0x0200,
		.config_cmd_enable = 1u,
		.config_mode_type[0] = DEVICE_CONFIG_CMD_TYPE_GENERIC,
		.config_cmd_seqs[0] = {
			.seq_num = 1u,
			.seq_id = 2u,
		},
		.device_type = FLEXSPI_DEVICE_TYPE_SERIAL_NOR,
		.sflash_pad_type = SERIAL_FLASH_4_PADS,
		.serial_clk_freq = FLEXSPI_SERIAL_CLK_133MHZ,
		.sflash_a1_size = 8u * 1024u * 1024u,
		.lookup_table = {
			/* 0: Fast Read Quad I/O */
			FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0xEB,
					RADDR_SDR, FLEXSPI_4PAD, 0x18),
			FLEXSPI_LUT_SEQ(DUMMY_SDR, FLEXSPI_4PAD, 0x06,
					READ_SDR, FLEXSPI_4PAD, 0x04),

			/* 1: Read Status Register 1 */
			[4 * 1 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0x05,
						      READ_SDR, FLEXSPI_1PAD, 0x02),

			/* 2: Read Status Register 2 */
			[4 * 2 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0x35,
						      DUMMY_SDR, FLEXSPI_1PAD, 0x08),

			/* 3: Write Enable */
			[4 * 3 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0x06,
						      STOP, FLEXSPI_1PAD, 0x00),

			/* 4: Write Status Registers 1 and 2 */
			[4 * 4 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0x01,
						      WRITE_SDR, FLEXSPI_1PAD, 0x02),

			/* 5: Sector Erase (4 KB) */
			[4 * 5 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0x20,
						      RADDR_SDR, FLEXSPI_1PAD, 0x18),

			/* 8: Block Erase (64 KB) */
			[4 * 8 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0xD8,
						      RADDR_SDR, FLEXSPI_1PAD, 0x18),

			/* 9: Page Program */
			[4 * 9 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0x02,
						      RADDR_SDR, FLEXSPI_1PAD, 0x18),
			[4 * 9 + 1] = FLEXSPI_LUT_SEQ(WRITE_SDR, FLEXSPI_1PAD, 0x04,
						      STOP, FLEXSPI_1PAD, 0x00),

			/* 11: Chip Erase */
			[4 * 11 + 0] = FLEXSPI_LUT_SEQ(CMD_SDR, FLEXSPI_1PAD, 0x60,
						       STOP, FLEXSPI_1PAD, 0x00),
		},
	},
	.page_size = 256u,
	.sector_size = 4u * 1024u,
	.ipcmd_serial_clk_freq = FLEXSPI_SERIAL_CLK_133MHZ,
	.block_size = 64u * 1024u,
	.is_uniform_block_size = false,
};
#endif /* CONFIG_NXP_IMXRT_BOOT_HEADER */
