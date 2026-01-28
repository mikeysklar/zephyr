/*
 * Copyright (c) 2024 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/tracing/tracing_format.h>
#include <string.h>
#include <stdio.h>
#include <pb_encode.h>
#include "perfetto_encoder.h"
#include "tracing_core.h"
#ifdef CONFIG_TRACING_GPIO
#include "perfetto_gpio.h"
#endif
#include "proto/perfetto_trace.pb.h"

/* Sequence flags */
#define SEQ_INCREMENTAL_STATE_CLEARED 1
#define SEQ_NEEDS_INCREMENTAL_STATE   2

/* Interned string entry */
struct intern_entry {
	uint32_t hash;
	uint64_t iid;
	char name[32];
	bool used;
};

/* Interned string tables */
static struct intern_entry event_names[CONFIG_PERFETTO_MAX_INTERNED_STRINGS];
static struct intern_entry categories[CONFIG_PERFETTO_MAX_INTERNED_STRINGS];
static uint64_t next_event_name_iid = 1;
static uint64_t next_category_iid = 1;

/* Thread descriptor tracking - simple bitmap for common case */
#define MAX_TRACKED_THREADS 32
static struct k_thread *tracked_threads[MAX_TRACKED_THREADS];
static uint32_t tracked_thread_flags;

/* State tracking */
static bool encoder_initialized;
static bool started;

/* Output buffer for encoding */
#define ENCODE_BUFFER_SIZE 256
static uint8_t encode_buffer[ENCODE_BUFFER_SIZE];

#if DT_HAS_COMPAT_STATUS_OKAY(zephyr_native_pty_uart)
#define DT_DRV_COMPAT zephyr_native_pty_uart
struct uart_track_info {
	const struct device *dev;
	const char *name;
	uint64_t track_uuid_base;
};

#define UART_TRACK_INFO(inst) \
	{ \
		.dev = DEVICE_DT_GET(DT_DRV_INST(inst)), \
		.name = DT_NODE_FULL_NAME(DT_DRV_INST(inst)), \
		.track_uuid_base = UART_TRACK_UUID_BASE + \
				   ((uint64_t)DT_DEP_ORD(DT_DRV_INST(inst)) << 2), \
	},

static const struct uart_track_info uart_tracks[] = {
	DT_INST_FOREACH_STATUS_OKAY(UART_TRACK_INFO)
};

#define NUM_UART_TRACKS DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)
static bool uart_tracks_initialized;
#else
#define NUM_UART_TRACKS 0
#endif

/* Simple string hash function */
static uint32_t hash_string(const char *str)
{
	uint32_t hash = 5381;

	while (*str) {
		hash = ((hash << 5) + hash) + (uint8_t)*str++;
	}
	return hash;
}

/* Look up or create an interned string entry */
static uint64_t intern_string(struct intern_entry *table, size_t table_size,
			      uint64_t *next_iid, const char *name)
{
	if (name == NULL || name[0] == '\0') {
		return 0;
	}

	uint32_t hash = hash_string(name);

	/* Look for existing entry */
	for (size_t i = 0; i < table_size; i++) {
		if (table[i].used && table[i].hash == hash &&
		    strcmp(table[i].name, name) == 0) {
			return table[i].iid;
		}
	}

	/* Find empty slot */
	for (size_t i = 0; i < table_size; i++) {
		if (!table[i].used) {
			table[i].used = true;
			table[i].hash = hash;
			table[i].iid = (*next_iid)++;
			strncpy(table[i].name, name, sizeof(table[i].name) - 1);
			table[i].name[sizeof(table[i].name) - 1] = '\0';
			return table[i].iid;
		}
	}

	/* Table full - return 0 to indicate failure */
	return 0;
}

uint64_t perfetto_get_timestamp_ns(void)
{
	/* Use k_uptime_ticks and convert to nanoseconds */
	int64_t ticks = k_uptime_ticks();

	return (uint64_t)k_ticks_to_ns_floor64(ticks);
}

uint64_t perfetto_get_process_uuid(void)
{
	return PROCESS_UUID;
}

uint64_t perfetto_get_thread_uuid(struct k_thread *thread)
{
	/* Use thread pointer as UUID, offset from base to avoid conflicts */
	return THREAD_UUID_BASE + (uint64_t)(uintptr_t)thread;
}

uint64_t perfetto_get_isr_uuid(void)
{
	return ISR_TRACK_UUID;
}

uint64_t perfetto_get_trace_uuid(void)
{
	return TRACE_TRACK_UUID;
}

uint64_t perfetto_intern_event_name(const char *name)
{
	return intern_string(event_names, ARRAY_SIZE(event_names),
			     &next_event_name_iid, name);
}

uint64_t perfetto_intern_category(const char *category)
{
	return intern_string(categories, ARRAY_SIZE(categories),
			     &next_category_iid, category);
}

bool perfetto_thread_descriptor_emitted(struct k_thread *thread)
{
	for (int i = 0; i < MAX_TRACKED_THREADS; i++) {
		if (tracked_threads[i] == thread) {
			return (tracked_thread_flags & BIT(i)) != 0;
		}
	}
	return false;
}

void perfetto_mark_thread_descriptor_emitted(struct k_thread *thread)
{
	for (int i = 0; i < MAX_TRACKED_THREADS; i++) {
		if (tracked_threads[i] == thread) {
			tracked_thread_flags |= BIT(i);
			return;
		}
		if (tracked_threads[i] == NULL) {
			tracked_threads[i] = thread;
			tracked_thread_flags |= BIT(i);
			return;
		}
	}
	/* Table full - can't track more threads */
}

/* Encode a varint into buffer, return number of bytes written */
static size_t encode_varint(uint8_t *buf, uint64_t value)
{
	size_t i = 0;

	while (value >= 0x80) {
		buf[i++] = (uint8_t)(value | 0x80);
		value >>= 7;
	}
	buf[i++] = (uint8_t)value;
	return i;
}

/* Emit encoded packet wrapped in Trace.packet field to tracing backend.
 * The Trace message has: repeated TracePacket packet = 1;
 * So we emit field 1 (tag 0x0a = field 1, wire type 2) + length + packet data.
 * This allows concatenating multiple packets into a valid Trace message.
 */
static void emit_packet(const uint8_t *data, size_t len)
{
	uint8_t header[12]; /* field tag (1 byte) + length varint (up to 10 bytes) */
	size_t header_len = 0;

	/* Field 1, wire type 2 (length-delimited) = (1 << 3) | 2 = 0x0a */
	header[header_len++] = 0x0a;

	/* Encode packet length as varint */
	header_len += encode_varint(&header[header_len], len);

	/* Emit header then packet data */
	tracing_format_raw_data(header, (uint32_t)header_len);
	tracing_format_raw_data((uint8_t *)data, (uint32_t)len);
}

void perfetto_emit_process_descriptor(void)
{
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackDescriptor desc = perfetto_protos_TrackDescriptor_init_zero;
	perfetto_protos_ProcessDescriptor proc = perfetto_protos_ProcessDescriptor_init_zero;

	/* Set up process descriptor */
	proc.has_pid = true;
	proc.pid = 1; /* Zephyr is single-process */
	strncpy(proc.process_name, CONFIG_PERFETTO_PROCESS_NAME,
		sizeof(proc.process_name) - 1);
	proc.has_process_name = true;

	/* Set up track descriptor */
	desc.has_uuid = true;
	desc.uuid = PROCESS_UUID;
	strncpy(desc.name, CONFIG_PERFETTO_PROCESS_NAME, sizeof(desc.name) - 1);
	desc.has_name = true;
	desc.has_process = true;
	desc.process = proc;

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.has_sequence_flags = true;
	packet.sequence_flags = SEQ_INCREMENTAL_STATE_CLEARED;
	packet.which_data = perfetto_protos_TracePacket_track_descriptor_tag;
	packet.data.track_descriptor = desc;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

void perfetto_emit_isr_track_descriptor(void)
{
	if (!perfetto_start()) {
		return;
	}
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackDescriptor desc = perfetto_protos_TrackDescriptor_init_zero;

	/* Set up track descriptor for ISR */
	desc.has_uuid = true;
	desc.uuid = ISR_TRACK_UUID;
	desc.has_parent_uuid = true;
	desc.parent_uuid = PROCESS_UUID;
	strncpy(desc.name, "ISR", sizeof(desc.name) - 1);

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.which_data = perfetto_protos_TracePacket_track_descriptor_tag;
	packet.data.track_descriptor = desc;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

void perfetto_emit_thread_descriptor(struct k_thread *thread, const char *name)
{
	if (!perfetto_start()) {
		return;
	}
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackDescriptor desc = perfetto_protos_TrackDescriptor_init_zero;
	perfetto_protos_ThreadDescriptor thd = perfetto_protos_ThreadDescriptor_init_zero;

	/* Set up thread descriptor */
	thd.has_pid = true;
	thd.pid = 1;
	thd.has_tid = true;
	thd.tid = (int32_t)(uintptr_t)thread;
	if (name != NULL && name[0] != '\0') {
		strncpy(thd.thread_name, name, sizeof(thd.thread_name) - 1);
	} else {
		snprintf(thd.thread_name, sizeof(thd.thread_name), "thread_%p", thread);
	}
	thd.has_thread_name = true;

	/* Set up track descriptor */
	desc.has_uuid = true;
	desc.uuid = perfetto_get_thread_uuid(thread);
	desc.has_parent_uuid = true;
	desc.parent_uuid = PROCESS_UUID;
	strncpy(desc.name, thd.thread_name, sizeof(desc.name) - 1);
	desc.has_name = true;
	desc.has_thread = true;
	desc.thread = thd;

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.which_data = perfetto_protos_TracePacket_track_descriptor_tag;
	packet.data.track_descriptor = desc;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}

	perfetto_mark_thread_descriptor_emitted(thread);
}

/* Helper to emit interned data for new strings */
static void emit_interned_data_if_needed(uint64_t name_iid, uint64_t category_iid)
{
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_InternedData interned = perfetto_protos_InternedData_init_zero;
	bool need_emit = false;

	/* Check if we need to emit event name */
	if (name_iid > 0) {
		for (size_t i = 0; i < ARRAY_SIZE(event_names); i++) {
			if (event_names[i].used && event_names[i].iid == name_iid) {
				interned.event_names_count = 1;
				interned.event_names[0].has_iid = true;
				interned.event_names[0].iid = name_iid;
				strncpy(interned.event_names[0].name,
					event_names[i].name,
					sizeof(interned.event_names[0].name) - 1);
				interned.event_names[0].has_name = true;
				need_emit = true;
				break;
			}
		}
	}

	/* Check if we need to emit category */
	if (category_iid > 0) {
		for (size_t i = 0; i < ARRAY_SIZE(categories); i++) {
			if (categories[i].used && categories[i].iid == category_iid) {
				interned.event_categories_count = 1;
				interned.event_categories[0].has_iid = true;
				interned.event_categories[0].iid = category_iid;
				strncpy(interned.event_categories[0].name,
					categories[i].name,
					sizeof(interned.event_categories[0].name) - 1);
				interned.event_categories[0].has_name = true;
				need_emit = true;
				break;
			}
		}
	}

	if (!need_emit) {
		return;
	}

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.which_data = perfetto_protos_TracePacket_interned_data_tag;
	packet.data.interned_data = interned;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

void _perfetto_emit_slice_begin(uint64_t track_uuid, uint64_t name_iid, const char* name_buf, size_t name_len, uint64_t category_iid)
{
	if (!perfetto_start()) {
		return;
	}
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackEvent event = perfetto_protos_TrackEvent_init_zero;

	/* Emit interned data first if this is a new string */
	emit_interned_data_if_needed(name_iid, category_iid);

	/* Set up track event */
	event.has_type = true;
	event.type = perfetto_protos_TrackEvent_Type_TYPE_SLICE_BEGIN;
	event.has_track_uuid = true;
	event.track_uuid = track_uuid;
	if (category_iid > 0) {
		event.category_iids_count = 1;
		event.category_iids[0] = category_iid;
	}
	if (name_iid > 0) {
		event.which_name_field = perfetto_protos_TrackEvent_name_iid_tag;
		event.name_field.name_iid = name_iid;
	}
	if (name_len > 0) {
	    event.which_name_field = perfetto_protos_TrackEvent_name_tag;
	    size_t copy_len = MIN(name_len, sizeof(event.name_field.name) - 1);
	    strncpy(event.name_field.name, name_buf, copy_len);
	    event.name_field.name[copy_len] = '\0';
	}

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.has_sequence_flags = true;
	packet.sequence_flags = SEQ_NEEDS_INCREMENTAL_STATE;
	packet.which_data = perfetto_protos_TracePacket_track_event_tag;
	packet.data.track_event = event;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

void perfetto_emit_slice_begin(uint64_t track_uuid, uint64_t name_iid, uint64_t category_iid)
{
    _perfetto_emit_slice_begin(track_uuid, name_iid, NULL, 0, category_iid);
}

void perfetto_emit_slice_begin_string(uint64_t track_uuid, const char* name_buf, size_t name_len)
{
    _perfetto_emit_slice_begin(track_uuid, 0, name_buf, name_len, 0);
}

void perfetto_emit_slice_end(uint64_t track_uuid)
{
	if (!perfetto_start()) {
		return;
	}
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackEvent event = perfetto_protos_TrackEvent_init_zero;

	/* Set up track event */
	event.has_type = true;
	event.type = perfetto_protos_TrackEvent_Type_TYPE_SLICE_END;
	event.has_track_uuid = true;
	event.track_uuid = track_uuid;

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.has_sequence_flags = true;
	packet.sequence_flags = SEQ_NEEDS_INCREMENTAL_STATE;
	packet.which_data = perfetto_protos_TracePacket_track_event_tag;
	packet.data.track_event = event;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

static void emit_slice_begin_at(uint64_t track_uuid, const char *name_buf,
				size_t name_len, uint64_t timestamp_ns)
{
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackEvent event = perfetto_protos_TrackEvent_init_zero;

	/* Set up track event */
	event.has_type = true;
	event.type = perfetto_protos_TrackEvent_Type_TYPE_SLICE_BEGIN;
	event.has_track_uuid = true;
	event.track_uuid = track_uuid;
	if (name_len > 0) {
		event.which_name_field = perfetto_protos_TrackEvent_name_tag;
		size_t copy_len = MIN(name_len, sizeof(event.name_field.name) - 1);
		strncpy(event.name_field.name, name_buf, copy_len);
		event.name_field.name[copy_len] = '\0';
	}

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = timestamp_ns;
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.has_sequence_flags = true;
	packet.sequence_flags = SEQ_NEEDS_INCREMENTAL_STATE;
	packet.which_data = perfetto_protos_TracePacket_track_event_tag;
	packet.data.track_event = event;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

static void emit_slice_end_at(uint64_t track_uuid, uint64_t timestamp_ns)
{
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackEvent event = perfetto_protos_TrackEvent_init_zero;

	/* Set up track event */
	event.has_type = true;
	event.type = perfetto_protos_TrackEvent_Type_TYPE_SLICE_END;
	event.has_track_uuid = true;
	event.track_uuid = track_uuid;

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = timestamp_ns;
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.has_sequence_flags = true;
	packet.sequence_flags = SEQ_NEEDS_INCREMENTAL_STATE;
	packet.which_data = perfetto_protos_TracePacket_track_event_tag;
	packet.data.track_event = event;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

void perfetto_emit_slice_with_duration(uint64_t track_uuid, const char *name_buf,
				       size_t name_len, uint64_t start_ns,
				       uint64_t duration_ns)
{
	if (!perfetto_start()) {
		return;
	}

	emit_slice_begin_at(track_uuid, name_buf, name_len, start_ns);
	emit_slice_end_at(track_uuid, start_ns + duration_ns);
}

void perfetto_emit_instant(uint64_t track_uuid, uint64_t name_iid, uint64_t category_iid)
{
	if (!perfetto_start()) {
		return;
	}
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackEvent event = perfetto_protos_TrackEvent_init_zero;

	/* Emit interned data first if this is a new string */
	emit_interned_data_if_needed(name_iid, category_iid);

	/* Set up track event */
	event.has_type = true;
	event.type = perfetto_protos_TrackEvent_Type_TYPE_INSTANT;
	event.has_track_uuid = true;
	event.track_uuid = track_uuid;
	if (name_iid > 0) {
    	event.which_name_field = perfetto_protos_TrackEvent_name_iid_tag;
    	event.name_field.name_iid = name_iid;
	}
	if (category_iid > 0) {
		event.category_iids_count = 1;
		event.category_iids[0] = category_iid;
	}

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.has_sequence_flags = true;
	packet.sequence_flags = SEQ_NEEDS_INCREMENTAL_STATE;
	packet.which_data = perfetto_protos_TracePacket_track_event_tag;
	packet.data.track_event = event;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

void perfetto_encoder_init(void)
{
	if (encoder_initialized) {
		return;
	}

	/* Clear interned string tables */
	memset(event_names, 0, sizeof(event_names));
	memset(categories, 0, sizeof(categories));
	memset(tracked_threads, 0, sizeof(tracked_threads));
	tracked_thread_flags = 0;
	next_event_name_iid = 1;
	next_category_iid = 1;
	started = false;

	/* Pre-intern common categories */
	(void)perfetto_intern_category("kernel");
	(void)perfetto_intern_category("thread");
	(void)perfetto_intern_category("isr");
	(void)perfetto_intern_category("sync");
#ifdef CONFIG_PERFETTO_GPIO_TRACING
	(void)perfetto_intern_category("gpio");
#endif

	encoder_initialized = true;
}

void perfetto_emit_track_descriptor(uint64_t track_uuid,
				    uint64_t parent_uuid,
				    const char *name)
{
	if (!perfetto_start()) {
		return;
	}
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackDescriptor desc = perfetto_protos_TrackDescriptor_init_zero;

	/* Set up track descriptor */
	desc.has_uuid = true;
	desc.uuid = track_uuid;
	if (parent_uuid != 0) {
		desc.has_parent_uuid = true;
		desc.parent_uuid = parent_uuid;
	}
	if (name != NULL && name[0] != '\0') {
		strncpy(desc.name, name, sizeof(desc.name) - 1);
		desc.has_name = true;
	}

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.which_data = perfetto_protos_TracePacket_track_descriptor_tag;
	packet.data.track_descriptor = desc;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

void perfetto_emit_counter_track_descriptor(uint64_t track_uuid,
					    uint64_t parent_uuid,
					    const char *name)
{
	if (!perfetto_start()) {
		return;
	}
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackDescriptor desc = perfetto_protos_TrackDescriptor_init_zero;
	perfetto_protos_CounterDescriptor counter = perfetto_protos_CounterDescriptor_init_zero;

	/* Set up counter descriptor - unit is COUNT for GPIO (0/1 values) */
	counter.has_unit = true;
	counter.unit = perfetto_protos_CounterDescriptor_Unit_UNIT_COUNT;

	/* Set up track descriptor */
	desc.has_uuid = true;
	desc.uuid = track_uuid;
	desc.has_parent_uuid = true;
	desc.parent_uuid = parent_uuid;
	if (name != NULL && name[0] != '\0') {
		strncpy(desc.name, name, sizeof(desc.name) - 1);
		desc.has_name = true;
	}
	desc.has_counter = true;
	desc.counter = counter;

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.which_data = perfetto_protos_TracePacket_track_descriptor_tag;
	packet.data.track_descriptor = desc;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

void perfetto_emit_counter(uint64_t track_uuid, int64_t value)
{
	perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;
	perfetto_protos_TrackEvent event = perfetto_protos_TrackEvent_init_zero;

	/* Set up track event */
	event.has_type = true;
	event.type = perfetto_protos_TrackEvent_Type_TYPE_COUNTER;
	event.has_track_uuid = true;
	event.track_uuid = track_uuid;
	event.which_counter_value_field = perfetto_protos_TrackEvent_counter_value_tag;
	event.counter_value_field.counter_value = value;

	/* Set up trace packet */
	packet.has_timestamp = true;
	packet.timestamp = perfetto_get_timestamp_ns();
	packet.has_trusted_packet_sequence_id = true;
	packet.trusted_packet_sequence_id = CONFIG_PERFETTO_TRUSTED_SEQUENCE_ID;
	packet.has_sequence_flags = true;
	packet.sequence_flags = SEQ_NEEDS_INCREMENTAL_STATE;
	packet.which_data = perfetto_protos_TracePacket_track_event_tag;
	packet.data.track_event = event;

	/* Encode and emit */
	pb_ostream_t stream = pb_ostream_from_buffer(encode_buffer, sizeof(encode_buffer));

	if (pb_encode(&stream, perfetto_protos_TracePacket_fields, &packet)) {
		emit_packet(encode_buffer, stream.bytes_written);
	}
}

uint64_t perfetto_get_uart_track_uuid(uint32_t dev_index)
{
	return UART_TRACK_UUID_BASE + dev_index;
}

#if NUM_UART_TRACKS > 0
static int find_uart_track_index(const struct device *dev)
{
	for (size_t i = 0; i < NUM_UART_TRACKS; i++) {
		if (uart_tracks[i].dev == dev) {
			return (int)i;
		}
	}

	return -1;
}
#endif

bool perfetto_get_uart_track_uuids(const struct device *dev,
				   uint64_t *device_uuid,
				   uint64_t *tx_uuid,
				   uint64_t *rx_uuid)
{
#if NUM_UART_TRACKS > 0
	int idx = find_uart_track_index(dev);

	if (idx < 0) {
		return false;
	}

	uint64_t base = uart_tracks[idx].track_uuid_base;

	if (device_uuid != NULL) {
		*device_uuid = base;
	}
	if (tx_uuid != NULL) {
		*tx_uuid = base + 1;
	}
	if (rx_uuid != NULL) {
		*rx_uuid = base + 2;
	}

	return true;
#else
	ARG_UNUSED(dev);
	ARG_UNUSED(device_uuid);
	ARG_UNUSED(tx_uuid);
	ARG_UNUSED(rx_uuid);
	return false;
#endif
}

static void perfetto_uart_init_tracks(void)
{
#if NUM_UART_TRACKS > 0
	if (uart_tracks_initialized) {
		return;
	}

	for (size_t idx = 0; idx < NUM_UART_TRACKS; idx++) {
		uint64_t base = uart_tracks[idx].track_uuid_base;

		perfetto_emit_track_descriptor(base, UART_GROUP_TRACK_UUID,
					       uart_tracks[idx].name);
		perfetto_emit_track_descriptor(base + 1, base, "TX");
		perfetto_emit_track_descriptor(base + 2, base, "RX");
	}

	uart_tracks_initialized = true;
#endif
}

bool perfetto_start(void)
{
	if (started) {
		return true;
	}
	if (!is_tracing_enabled()) {
		return false;
	}
	started = true;
	perfetto_emit_process_descriptor();

	/* Emit top-level "Trace" track under the process */
	perfetto_emit_track_descriptor(TRACE_TRACK_UUID, PROCESS_UUID, "Trace");

#ifdef CONFIG_TRACING_GPIO
	/* Initialize GPIO counter tracks from device tree */
	perfetto_gpio_init_tracks();
#endif

#ifdef CONFIG_EMUL
	/* Emit top-level "Emulated" track under the process */
	perfetto_emit_track_descriptor(EMULATED_TRACK_UUID, PROCESS_UUID, "Emulated");

	/* Emit UART group track under Emulated */
	perfetto_emit_track_descriptor(UART_GROUP_TRACK_UUID, EMULATED_TRACK_UUID, "UART");

	perfetto_uart_init_tracks();
#endif
	return true;
}
