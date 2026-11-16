/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/arch/riscv/csr.h>

#ifndef CONFIG_VECTOR_TABLE_SIZE
#error "VECTOR_TABLE_SIZE must be defined"
#endif

/* QingKe V3V custom CSRs (CH32V407 RM, table 9-3). */
#define CSR_HW_POPDM_CTLR 0x804
#define CSR_CPU_RUN_CTLR  0xbc0
#define CSR_INEST_CTLR    0xbc1

/* PFIC global interrupt status (RM 9.4.5.11) */
#define PFIC_GISR         0xe000e04c
#define PFIC_GISR_GACTSTA 0x100

/* Exports */
GTEXT(__start)

/* Imports */
GTEXT(__initialize)

SECTION_FUNC(vectors, ivt)
	.option norvc
	/* A bootloader enters through the flash alias at 0, so jump relative first. */
	j       __start
	.rept   CONFIG_VECTOR_TABLE_SIZE
	.word	_isr_wrapper
	.endr

SECTION_FUNC(vectors, __start)
	/* Interrupts off until Zephyr is ready for them. */
	csrci	mstatus, 0x8

	/* Continue at the link address in flash at 0x08000000 (RM 1.3). */
	lui	t0, %hi(1f)
	jr	%lo(1f)(t0)
1:

	/*
	 * A bootloader may have enabled hardware stacking and nesting, which
	 * Zephyr's ISR entry does not expect. Restore the reset values.
	 */
	li	t0, 0x4
	csrw	CSR_HW_POPDM_CTLR, t0
	csrw	CSR_INEST_CTLR, zero

	/* Enable instruction prefetch. */
	li	t0, 0x1
	csrw	CSR_CPU_RUN_CTLR, t0

	/* Vectored mode, table entries are absolute addresses. */
	la	t0, ivt
	ori	t0, t0, 3
	csrw	mtvec, t0

	/*
	 * A bootloader may start the application from inside an interrupt
	 * handler (TinyUF2 uses its software interrupt). The PFIC then still
	 * marks that interrupt active and masks every interrupt of equal or
	 * lower priority, so leave it with mret. Otherwise jump directly: an
	 * mret with no active interrupt leaves the PFIC unable to take
	 * interrupts.
	 */
	li	t0, PFIC_GISR
	lw	t0, 0(t0)
	andi	t0, t0, PFIC_GISR_GACTSTA
	beqz	t0, 2f

	la	t0, __initialize
	csrw	mepc, t0
	li	t0, MSTATUS_MPP
	csrs	mstatus, t0
	li	t0, (MSTATUS_MIE | MSTATUS_MPIE)
	csrc	mstatus, t0
	mret

2:
	la	t0, __initialize
	jr	t0
