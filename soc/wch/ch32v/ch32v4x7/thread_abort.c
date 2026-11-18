/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/arch/riscv/csr.h>
#include <zephyr/sys/__assert.h>
#include <ksched.h>

#include <hal_ch32fun.h>

/* PFIC global interrupt status: an interrupt or exception is active (RM 9.4.5.11) */
#define PFIC_GISR_GACTSTA BIT(8)

/*
 * Leave the active exception without returning from it: mret to the next
 * instruction, staying in machine mode with interrupts off. The exception
 * frame already holds mepc and mstatus, so both can be reused here.
 */
static ALWAYS_INLINE void leave_exception(void)
{
	csr_set(mstatus, MSTATUS_MPP);
	csr_clear(mstatus, MSTATUS_MPIE);
	__asm__ volatile("la t0, 1f\n"
			 "csrw mepc, t0\n"
			 "mret\n"
			 "1:\n"
			 :
			 :
			 : "t0", "memory");
}

/*
 * A thread that aborts itself from a fatal error handler is still inside the
 * exception. With RISCV_ALWAYS_SWITCH_THROUGH_ECALL the switch away from it
 * is an ecall, and the core cannot take an exception while one is active: it
 * locks up and the SoC resets. Leave the exception first. The aborted thread
 * never runs the exception exit path, so nothing else would do it.
 */
void z_impl_k_thread_abort(k_tid_t thread)
{
	SYS_PORT_TRACING_OBJ_FUNC_ENTER(k_thread, abort, thread);

	if ((thread == _current) && !arch_is_in_isr() && ((PFIC->GISR & PFIC_GISR_GACTSTA) != 0U)) {
		leave_exception();
	}

	z_thread_abort(thread);

	__ASSERT_NO_MSG(z_is_thread_dead(thread));

	SYS_PORT_TRACING_OBJ_FUNC_EXIT(k_thread, abort, thread);
}
