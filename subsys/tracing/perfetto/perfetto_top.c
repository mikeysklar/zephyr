/*
 * Copyright (c) 2024 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <kernel_internal.h>
#include <zephyr/init.h>
#include <string.h>

#include "perfetto_encoder.h"
#include "tracing_perfetto.h"

/* Category IIDs for common categories */
static uint64_t cat_kernel_iid;
static uint64_t cat_thread_iid;
static uint64_t cat_isr_iid;
static uint64_t cat_sync_iid;

/* Event name IIDs */
static uint64_t ev_thread_running_iid;
static uint64_t ev_isr_iid;
static uint64_t ev_idle_iid;
static uint64_t ev_sem_take_iid;
static uint64_t ev_sem_give_iid;
static uint64_t ev_mutex_lock_iid;
static uint64_t ev_mutex_unlock_iid;

/* State tracking */
static bool perfetto_initialized;

/* ISR track descriptor emitted flag */
static bool isr_track_emitted;

/* Helper to get thread name */
static const char *get_thread_name(struct k_thread *thread)
{
	const char *name = k_thread_name_get(thread);

	if (name == NULL || name[0] == '\0') {
		return NULL;
	}
	return name;
}

/* Forward declaration for ISR track emission */
void perfetto_emit_isr_track_descriptor(void);

/* Emit ISR track descriptor if not already done */
static void ensure_isr_track(void)
{
	if (isr_track_emitted) {
		return;
	}

	perfetto_emit_isr_track_descriptor();
	isr_track_emitted = true;
}

/* Initialize Perfetto tracing */
static int perfetto_init(void)
{
	if (perfetto_initialized) {
		return 0;
	}

	/* Initialize the encoder */
	perfetto_encoder_init();

	/* Cache category IIDs */
	cat_kernel_iid = perfetto_intern_category("kernel");
	cat_thread_iid = perfetto_intern_category("thread");
	cat_isr_iid = perfetto_intern_category("isr");
	cat_sync_iid = perfetto_intern_category("sync");

	/* Cache common event name IIDs */
	ev_thread_running_iid = perfetto_intern_event_name("Running");
	ev_isr_iid = perfetto_intern_event_name("ISR");
	ev_idle_iid = perfetto_intern_event_name("Idle");
	ev_sem_take_iid = perfetto_intern_event_name("sem_take");
	ev_sem_give_iid = perfetto_intern_event_name("sem_give");
	ev_mutex_lock_iid = perfetto_intern_event_name("mutex_lock");
	ev_mutex_unlock_iid = perfetto_intern_event_name("mutex_unlock");

	perfetto_initialized = true;

	return 0;
}

SYS_INIT(perfetto_init, POST_KERNEL, 0);

/* Thread create hook */
void sys_trace_k_thread_create(struct k_thread *new_thread, size_t stack_size, int prio)
{
	ARG_UNUSED(stack_size);
	ARG_UNUSED(prio);

	if (!perfetto_initialized) {
	    return;
	}

	const char *name = get_thread_name(new_thread);

	/* Emit thread descriptor */
	if (!perfetto_thread_descriptor_emitted(new_thread)) {
		perfetto_emit_thread_descriptor(new_thread, name);
	}
}

/* Thread name set hook */
void sys_trace_k_thread_name_set(struct k_thread *thread, int ret)
{
	ARG_UNUSED(ret);

	if (!perfetto_initialized) {
		return;
	}

	const char *name = get_thread_name(thread);

	/* Re-emit thread descriptor with new name */
	perfetto_emit_thread_descriptor(thread, name);
}

/* Thread switched out hook */
void sys_trace_k_thread_switched_out(void)
{
	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	/* End the "Running" slice for this thread */
	perfetto_emit_slice_end(track_uuid);
}

/* Thread switched in hook */
void sys_trace_k_thread_switched_in(void)
{
	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	/* Ensure thread descriptor is emitted */
	if (!perfetto_thread_descriptor_emitted(thread)) {
		const char *name = get_thread_name(thread);

		perfetto_emit_thread_descriptor(thread, name);
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	/* Start a "Running" slice for this thread */
	perfetto_emit_slice_begin(track_uuid, ev_thread_running_iid, cat_thread_iid);
}

/* ISR enter hook */
void sys_trace_isr_enter(void)
{
	if (!perfetto_initialized) {
		return;
	}

	ensure_isr_track();

	/* Start ISR slice */
	perfetto_emit_slice_begin(perfetto_get_isr_uuid(), ev_isr_iid, cat_isr_iid);
}

/* ISR exit hook */
void sys_trace_isr_exit(void)
{
	if (!perfetto_initialized) {
		return;
	}

	/* End ISR slice */
	perfetto_emit_slice_end(perfetto_get_isr_uuid());
}

/* Idle enter hook */
void sys_trace_idle(void)
{
	if (!perfetto_initialized) {
		return;
	}

	/* Emit idle instant event on process track */
	perfetto_emit_instant(perfetto_get_process_uuid(), ev_idle_iid, cat_kernel_iid);
}

/* Idle exit hook */
void sys_trace_idle_exit(void)
{
	/* Nothing to do - wakeup handled by thread switch */
}

/* Semaphore init */
void sys_trace_k_sem_init(struct k_sem *sem, int ret)
{
	ARG_UNUSED(sem);
	ARG_UNUSED(ret);
	/* Could emit an instant event, but skipping for now */
}

/* Semaphore give enter */
void sys_trace_k_sem_give_enter(struct k_sem *sem)
{
	ARG_UNUSED(sem);

	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	perfetto_emit_slice_begin(track_uuid, ev_sem_give_iid, cat_sync_iid);
}

/* Semaphore give exit */
void sys_trace_k_sem_give_exit(struct k_sem *sem)
{
	ARG_UNUSED(sem);

	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	perfetto_emit_slice_end(track_uuid);
}

/* Semaphore take enter */
void sys_trace_k_sem_take_enter(struct k_sem *sem, k_timeout_t timeout)
{
	ARG_UNUSED(sem);
	ARG_UNUSED(timeout);

	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	perfetto_emit_slice_begin(track_uuid, ev_sem_take_iid, cat_sync_iid);
}

/* Semaphore take blocking */
void sys_trace_k_sem_take_blocking(struct k_sem *sem, k_timeout_t timeout)
{
	ARG_UNUSED(sem);
	ARG_UNUSED(timeout);
	/* Could add a marker for blocking, but the slice already covers it */
}

/* Semaphore take exit */
void sys_trace_k_sem_take_exit(struct k_sem *sem, k_timeout_t timeout, int ret)
{
	ARG_UNUSED(sem);
	ARG_UNUSED(timeout);
	ARG_UNUSED(ret);

	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	perfetto_emit_slice_end(track_uuid);
}

/* Mutex init */
void sys_trace_k_mutex_init(struct k_mutex *mutex, int ret)
{
	ARG_UNUSED(mutex);
	ARG_UNUSED(ret);
}

/* Mutex lock enter */
void sys_trace_k_mutex_lock_enter(struct k_mutex *mutex, k_timeout_t timeout)
{
	ARG_UNUSED(mutex);
	ARG_UNUSED(timeout);

	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	perfetto_emit_slice_begin(track_uuid, ev_mutex_lock_iid, cat_sync_iid);
}

/* Mutex lock blocking */
void sys_trace_k_mutex_lock_blocking(struct k_mutex *mutex, k_timeout_t timeout)
{
	ARG_UNUSED(mutex);
	ARG_UNUSED(timeout);
}

/* Mutex lock exit */
void sys_trace_k_mutex_lock_exit(struct k_mutex *mutex, k_timeout_t timeout, int ret)
{
	ARG_UNUSED(mutex);
	ARG_UNUSED(timeout);
	ARG_UNUSED(ret);

	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	perfetto_emit_slice_end(track_uuid);
}

/* Mutex unlock enter */
void sys_trace_k_mutex_unlock_enter(struct k_mutex *mutex)
{
	ARG_UNUSED(mutex);

	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	perfetto_emit_slice_begin(track_uuid, ev_mutex_unlock_iid, cat_sync_iid);
}

/* Mutex unlock exit */
void sys_trace_k_mutex_unlock_exit(struct k_mutex *mutex, int ret)
{
	ARG_UNUSED(mutex);
	ARG_UNUSED(ret);

	if (!perfetto_initialized) {
		return;
	}

	struct k_thread *thread = k_sched_current_thread_query();

	if (thread == NULL) {
		return;
	}

	uint64_t track_uuid = perfetto_get_thread_uuid(thread);

	perfetto_emit_slice_end(track_uuid);
}
