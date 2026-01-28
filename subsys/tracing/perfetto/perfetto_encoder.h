/*
 * Copyright (c) 2024 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SUBSYS_TRACING_PERFETTO_ENCODER_H
#define SUBSYS_TRACING_PERFETTO_ENCODER_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Track UUIDs - use fixed values for known tracks */
#define PROCESS_UUID    1ULL
#define ISR_TRACK_UUID  2ULL
#define THREAD_UUID_BASE 0x1000ULL
#define TRACE_TRACK_UUID 3ULL
#define EMULATED_TRACK_UUID 4ULL
#define UART_GROUP_TRACK_UUID 5ULL
#define UART_TRACK_UUID_BASE 0x2000ULL

/**
 * @brief Initialize the Perfetto encoder
 *
 * Sets up interned string tables and emits initial track descriptors.
 */
void perfetto_encoder_init(void);

/**
 * @brief Get or create an interned ID for an event name
 *
 * @param name The event name string
 * @return Interned ID (1-based), or 0 if interning failed
 */
uint64_t perfetto_intern_event_name(const char *name);

/**
 * @brief Get or create an interned ID for an event category
 *
 * @param category The category string
 * @return Interned ID (1-based), or 0 if interning failed
 */
uint64_t perfetto_intern_category(const char *category);

/**
 * @brief Emit a track descriptor for a thread
 *
 * @param thread Pointer to the k_thread structure
 * @param name Thread name (may be NULL)
 */
void perfetto_emit_thread_descriptor(struct k_thread *thread, const char *name);

/**
 * @brief Emit the process descriptor
 *
 * Should be called once at initialization.
 */
void perfetto_emit_process_descriptor(void);

/**
 * @brief Emit a slice begin event
 *
 * @param track_uuid Track UUID (typically thread pointer)
 * @param name_iid Interned event name ID
 * @param category_iid Interned category ID (0 if none)
 */
void perfetto_emit_slice_begin(uint64_t track_uuid, uint64_t name_iid, uint64_t category_iid);

void perfetto_emit_slice_begin_string(uint64_t track_uuid, const char* buf, size_t len);

/**
 * @brief Emit a slice end event
 *
 * @param track_uuid Track UUID (typically thread pointer)
 */
void perfetto_emit_slice_end(uint64_t track_uuid);

/**
 * @brief Emit a complete slice with known duration
 *
 * This emits both begin and end events with explicit timestamps,
 * useful when the duration is known ahead of time (e.g., UART transmission).
 *
 * @param track_uuid Track UUID for the slice
 * @param name_buf Event name string
 * @param name_len Length of name string
 * @param start_ns Start timestamp in nanoseconds
 * @param duration_ns Duration in nanoseconds
 */
void perfetto_emit_slice_with_duration(uint64_t track_uuid, const char *name_buf,
				       size_t name_len, uint64_t start_ns,
				       uint64_t duration_ns);

/**
 * @brief Emit an instant event
 *
 * @param track_uuid Track UUID (typically thread pointer)
 * @param name_iid Interned event name ID
 * @param category_iid Interned category ID (0 if none)
 */
void perfetto_emit_instant(uint64_t track_uuid, uint64_t name_iid, uint64_t category_iid);

/**
 * @brief Get current timestamp in nanoseconds
 *
 * @return Timestamp suitable for Perfetto trace packets
 */
uint64_t perfetto_get_timestamp_ns(void);

/**
 * @brief Get the process track UUID
 *
 * @return Process track UUID
 */
uint64_t perfetto_get_process_uuid(void);

/**
 * @brief Get or create a track UUID for a thread
 *
 * @param thread Pointer to the k_thread structure
 * @return Track UUID for this thread
 */
uint64_t perfetto_get_thread_uuid(struct k_thread *thread);

/**
 * @brief Get the ISR track UUID
 *
 * @return Track UUID for ISR events
 */
uint64_t perfetto_get_isr_uuid(void);

/**
 * @brief Get the top-level Trace track UUID
 *
 * @return Track UUID for the Trace group track
 */
uint64_t perfetto_get_trace_uuid(void);

/**
 * @brief Emit the ISR track descriptor
 *
 * Should be called once before emitting ISR events.
 */
void perfetto_emit_isr_track_descriptor(void);

/**
 * @brief Get a unique track UUID for a UART device
 *
 * @param dev_index Device instance index (e.g., from DT_INST)
 * @return Track UUID for this UART device
 */
uint64_t perfetto_get_uart_track_uuid(uint32_t dev_index);

/**
 * @brief Get UART track UUIDs for a device
 *
 * @param dev UART device pointer
 * @param device_uuid Returned UUID for the UART device track (may be NULL)
 * @param tx_uuid Returned UUID for the UART TX subtrack (may be NULL)
 * @param rx_uuid Returned UUID for the UART RX subtrack (may be NULL)
 * @return true if the device was found in the UART track table
 */
bool perfetto_get_uart_track_uuids(const struct device *dev,
				   uint64_t *device_uuid,
				   uint64_t *tx_uuid,
				   uint64_t *rx_uuid);

/**
 * @brief Emit a track descriptor for grouping tracks
 *
 * @param track_uuid Unique track UUID
 * @param parent_uuid Parent track UUID (0 for no parent)
 * @param name Human-readable name for the track
 */
void perfetto_emit_track_descriptor(uint64_t track_uuid,
				    uint64_t parent_uuid,
				    const char *name);

/**
 * @brief Emit a counter track descriptor
 *
 * @param track_uuid Unique track UUID for this counter
 * @param parent_uuid Parent track UUID (typically process UUID)
 * @param name Human-readable name for the counter track
 */
void perfetto_emit_counter_track_descriptor(uint64_t track_uuid,
					    uint64_t parent_uuid,
					    const char *name);

/**
 * @brief Emit a counter value event
 *
 * @param track_uuid Track UUID for the counter
 * @param value Counter value (0 or 1 for GPIO)
 */
void perfetto_emit_counter(uint64_t track_uuid, int64_t value);

/**
 * @brief Start Perfetto tracing
 *
 * Emits the process descriptor and any pre-configured track descriptors.
 * Should be called before emitting any trace events. Only starts if
 * tracing is enabled via is_tracing_enabled().
 *
 * @return true if tracing is started and enabled, false if not enabled
 */
bool perfetto_start(void);

/**
 * @brief Check if a thread descriptor has been emitted
 *
 * @param thread Pointer to the k_thread structure
 * @return true if descriptor was already emitted
 */
bool perfetto_thread_descriptor_emitted(struct k_thread *thread);

/**
 * @brief Mark a thread descriptor as emitted
 *
 * @param thread Pointer to the k_thread structure
 */
void perfetto_mark_thread_descriptor_emitted(struct k_thread *thread);

#ifdef __cplusplus
}
#endif

#endif /* SUBSYS_TRACING_PERFETTO_ENCODER_H */
