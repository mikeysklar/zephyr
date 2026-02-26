/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nsi_perfetto_trace.h"

#include <pb_decode.h>
#include "perfetto_trace.pb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nsi_perfetto_event_int {
	uint64_t ts_ns;
	int64_t value;
};

struct nsi_perfetto_event_double {
	uint64_t ts_ns;
	double value;
};

struct nsi_perfetto_event_instant {
	uint64_t ts_ns;
};

struct nsi_perfetto_track {
	uint64_t uuid;
	char name[64];
	struct nsi_perfetto_event_int *int_events;
	size_t int_count;
	size_t int_capacity;
	struct nsi_perfetto_event_double *double_events;
	size_t double_count;
	size_t double_capacity;
	struct nsi_perfetto_event_instant *instant_events;
	size_t instant_count;
	size_t instant_capacity;
};

static struct nsi_perfetto_track *tracks;
static size_t track_count;
static size_t track_capacity;
static const char *trace_path;
static bool trace_loaded;
static uint64_t trace_min_timestamp = UINT64_MAX;
static bool trace_offset_initialized;
static uint64_t trace_time_offset;

void nsi_perfetto_trace_set_path(const char *path)
{
	trace_path = path;
}

static struct nsi_perfetto_track *find_track_by_uuid(uint64_t uuid)
{
	for (size_t i = 0; i < track_count; i++) {
		if (tracks[i].uuid == uuid) {
			return &tracks[i];
		}
	}
	return NULL;
}

static struct nsi_perfetto_track *find_track_by_name(const char *name)
{
	for (size_t i = 0; i < track_count; i++) {
		if (strcmp(tracks[i].name, name) == 0) {
			return &tracks[i];
		}
	}
	return NULL;
}

static struct nsi_perfetto_track *ensure_track(uint64_t uuid, const char *name)
{
	struct nsi_perfetto_track *track = find_track_by_uuid(uuid);

	if (track) {
		return track;
	}

	if (track_count == track_capacity) {
		size_t new_capacity = track_capacity ? track_capacity * 2 : 8;
		void *new_tracks = realloc(tracks, new_capacity * sizeof(*tracks));
		if (!new_tracks) {
			return NULL;
		}
		tracks = new_tracks;
		track_capacity = new_capacity;
	}

	track = &tracks[track_count++];
	memset(track, 0, sizeof(*track));
	track->uuid = uuid;
	strncpy(track->name, name, sizeof(track->name) - 1);
	track->name[sizeof(track->name) - 1] = '\0';
	return track;
}

static bool track_add_int_event(struct nsi_perfetto_track *track, uint64_t ts_ns, int64_t value)
{
	if (track->int_count == track->int_capacity) {
		size_t new_capacity = track->int_capacity ? track->int_capacity * 2 : 16;
		void *new_events = realloc(track->int_events,
					   new_capacity * sizeof(*track->int_events));
		if (!new_events) {
			return false;
		}
		track->int_events = new_events;
		track->int_capacity = new_capacity;
	}
	track->int_events[track->int_count++] = (struct nsi_perfetto_event_int){ts_ns, value};
	if (ts_ns < trace_min_timestamp) {
		trace_min_timestamp = ts_ns;
	}
	return true;
}

static bool track_add_double_event(struct nsi_perfetto_track *track, uint64_t ts_ns, double value)
{
	if (track->double_count == track->double_capacity) {
		size_t new_capacity = track->double_capacity ? track->double_capacity * 2 : 16;
		void *new_events = realloc(track->double_events,
					   new_capacity * sizeof(*track->double_events));
		if (!new_events) {
			return false;
		}
		track->double_events = new_events;
		track->double_capacity = new_capacity;
	}
	track->double_events[track->double_count++] = (struct nsi_perfetto_event_double){ts_ns, value};
	if (ts_ns < trace_min_timestamp) {
		trace_min_timestamp = ts_ns;
	}
	return true;
}

static bool track_add_instant_event(struct nsi_perfetto_track *track, uint64_t ts_ns)
{
	if (track->instant_count == track->instant_capacity) {
		size_t new_capacity = track->instant_capacity ? track->instant_capacity * 2 : 16;
		void *new_events = realloc(track->instant_events,
					   new_capacity * sizeof(*track->instant_events));
		if (!new_events) {
			return false;
		}
		track->instant_events = new_events;
		track->instant_capacity = new_capacity;
	}
	track->instant_events[track->instant_count++] = (struct nsi_perfetto_event_instant){ts_ns};
	if (ts_ns < trace_min_timestamp) {
		trace_min_timestamp = ts_ns;
	}
	return true;
}

static int cmp_int_event(const void *a, const void *b)
{
	const struct nsi_perfetto_event_int *ea = a;
	const struct nsi_perfetto_event_int *eb = b;

	if (ea->ts_ns < eb->ts_ns) {
		return -1;
	}
	if (ea->ts_ns > eb->ts_ns) {
		return 1;
	}
	return 0;
}

static int cmp_double_event(const void *a, const void *b)
{
	const struct nsi_perfetto_event_double *ea = a;
	const struct nsi_perfetto_event_double *eb = b;

	if (ea->ts_ns < eb->ts_ns) {
		return -1;
	}
	if (ea->ts_ns > eb->ts_ns) {
		return 1;
	}
	return 0;
}

static int cmp_instant_event(const void *a, const void *b)
{
	const struct nsi_perfetto_event_instant *ea = a;
	const struct nsi_perfetto_event_instant *eb = b;

	if (ea->ts_ns < eb->ts_ns) {
		return -1;
	}
	if (ea->ts_ns > eb->ts_ns) {
		return 1;
	}
	return 0;
}

static void sort_track_events(struct nsi_perfetto_track *track)
{
	if (track->int_count > 1) {
		qsort(track->int_events, track->int_count, sizeof(*track->int_events),
		      cmp_int_event);
	}
	if (track->double_count > 1) {
		qsort(track->double_events, track->double_count,
		      sizeof(*track->double_events), cmp_double_event);
	}
	if (track->instant_count > 1) {
		qsort(track->instant_events, track->instant_count,
		      sizeof(*track->instant_events), cmp_instant_event);
	}
}

static void sort_all_tracks(void)
{
	for (size_t i = 0; i < track_count; i++) {
		sort_track_events(&tracks[i]);
	}
}

static void parse_packet(const perfetto_protos_TracePacket *packet)
{
	if (packet->which_data == perfetto_protos_TracePacket_track_descriptor_tag) {
		const perfetto_protos_TrackDescriptor *desc = &packet->data.track_descriptor;

		if (!desc->has_uuid || !desc->has_name) {
			return;
		}
		(void)ensure_track(desc->uuid, desc->name);
		return;
	}

	if (packet->which_data != perfetto_protos_TracePacket_track_event_tag) {
		return;
	}

	const perfetto_protos_TrackEvent *event = &packet->data.track_event;
	uint64_t ts_ns = packet->has_timestamp ? packet->timestamp : 0;

	if (!event->has_track_uuid || !event->has_type) {
		return;
	}

	struct nsi_perfetto_track *track = find_track_by_uuid(event->track_uuid);
	if (!track) {
		return;
	}

	if (event->type == perfetto_protos_TrackEvent_Type_TYPE_INSTANT) {
		(void)track_add_instant_event(track, ts_ns);
		return;
	}

	if (event->type != perfetto_protos_TrackEvent_Type_TYPE_COUNTER) {
		return;
	}

	if (event->which_counter_value_field == perfetto_protos_TrackEvent_counter_value_tag) {
		(void)track_add_int_event(track, ts_ns,
					  event->counter_value_field.counter_value);
	} else if (event->which_counter_value_field ==
		   perfetto_protos_TrackEvent_double_counter_value_tag) {
		(void)track_add_double_event(track, ts_ns,
					     event->counter_value_field.double_counter_value);
	}
}

static bool load_trace(void)
{
	FILE *f = NULL;
	uint8_t *buffer = NULL;
	long size = 0;
	bool ok = false;

	if (!trace_path) {
		return false;
	}

	f = fopen(trace_path, "rb");
	if (!f) {
		return false;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		goto out;
	}
	size = ftell(f);
	if (size <= 0) {
		goto out;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		goto out;
	}

	buffer = malloc((size_t)size);
	if (!buffer) {
		goto out;
	}
	if (fread(buffer, 1, (size_t)size, f) != (size_t)size) {
		goto out;
	}

	pb_istream_t stream = pb_istream_from_buffer(buffer, (size_t)size);
	while (stream.bytes_left > 0) {
		bool eof = false;
		pb_wire_type_t wire_type = 0;
		uint32_t tag = 0;
		perfetto_protos_TracePacket packet = perfetto_protos_TracePacket_init_zero;

		if (!pb_decode_tag(&stream, &wire_type, &tag, &eof)) {
			if (eof) {
				break;
			}
			goto out;
		}
		if (tag != perfetto_protos_Trace_packet_tag || wire_type != PB_WT_STRING) {
			if (!pb_skip_field(&stream, wire_type)) {
				goto out;
			}
			continue;
		}

		pb_istream_t substream;
		if (!pb_make_string_substream(&stream, &substream)) {
			goto out;
		}
		if (pb_decode(&substream, perfetto_protos_TracePacket_fields, &packet)) {
			parse_packet(&packet);
		}
		pb_close_string_substream(&stream, &substream);
	}

	sort_all_tracks();
	ok = true;

out:
	free(buffer);
	fclose(f);
	return ok;
}

static void ensure_loaded(void)
{
	if (trace_loaded) {
		return;
	}
	if (!trace_path) {
		return;
	}
	trace_loaded = load_trace();
}

bool nsi_perfetto_trace_enabled(void)
{
	ensure_loaded();
	return trace_loaded;
}

static bool find_latest_int(const struct nsi_perfetto_track *track, uint64_t ts_ns,
			    int64_t *value)
{
	if (!track || track->int_count == 0) {
		return false;
	}

	size_t left = 0;
	size_t right = track->int_count;
	while (left < right) {
		size_t mid = left + (right - left) / 2;
		if (track->int_events[mid].ts_ns <= ts_ns) {
			left = mid + 1;
		} else {
			right = mid;
		}
	}
	if (left == 0) {
		return false;
	}
	*value = track->int_events[left - 1].value;
	return true;
}

static bool find_latest_double(const struct nsi_perfetto_track *track, uint64_t ts_ns,
			       double *value)
{
	if (!track || track->double_count == 0) {
		return false;
	}

	size_t left = 0;
	size_t right = track->double_count;
	while (left < right) {
		size_t mid = left + (right - left) / 2;
		if (track->double_events[mid].ts_ns <= ts_ns) {
			left = mid + 1;
		} else {
			right = mid;
		}
	}
	if (left == 0) {
		return false;
	}
	*value = track->double_events[left - 1].value;
	return true;
}

static bool find_next_int(const struct nsi_perfetto_track *track, uint64_t ts_ns,
		      uint64_t *next_ts_ns)
{
	if (!track || track->int_count == 0) {
		return false;
	}

	size_t left = 0;
	size_t right = track->int_count;
	while (left < right) {
		size_t mid = left + (right - left) / 2;
		if (track->int_events[mid].ts_ns <= ts_ns) {
			left = mid + 1;
		} else {
			right = mid;
		}
	}
	if (left >= track->int_count) {
		return false;
	}
	*next_ts_ns = track->int_events[left].ts_ns;
	return true;
}

static bool find_next_double(const struct nsi_perfetto_track *track, uint64_t ts_ns,
			 uint64_t *next_ts_ns)
{
	if (!track || track->double_count == 0) {
		return false;
	}

	size_t left = 0;
	size_t right = track->double_count;
	while (left < right) {
		size_t mid = left + (right - left) / 2;
		if (track->double_events[mid].ts_ns <= ts_ns) {
			left = mid + 1;
		} else {
			right = mid;
		}
	}
	if (left >= track->double_count) {
		return false;
	}
	*next_ts_ns = track->double_events[left].ts_ns;
	return true;
}

static uint64_t adjust_trace_timestamp(uint64_t timestamp_ns)
{
	if (!trace_offset_initialized) {
		if (trace_min_timestamp != UINT64_MAX && timestamp_ns > trace_min_timestamp) {
			trace_time_offset = timestamp_ns - trace_min_timestamp;
		} else {
			trace_time_offset = 0;
		}
		trace_offset_initialized = true;
	}
	if (trace_time_offset && timestamp_ns >= trace_time_offset) {
		return timestamp_ns - trace_time_offset;
	}
	return timestamp_ns;
}

bool nsi_perfetto_trace_get_counter_int(const char *track_name, uint64_t timestamp_ns,
					int64_t *value)
{
	ensure_loaded();
	return find_latest_int(find_track_by_name(track_name),
			       adjust_trace_timestamp(timestamp_ns), value);
}

bool nsi_perfetto_trace_get_counter_double(const char *track_name, uint64_t timestamp_ns,
					   double *value)
{
	ensure_loaded();
	return find_latest_double(find_track_by_name(track_name),
				  adjust_trace_timestamp(timestamp_ns), value);
}

static bool find_next_instant(const struct nsi_perfetto_track *track, uint64_t ts_ns,
			      uint64_t *next_ts_ns)
{
	if (!track || track->instant_count == 0) {
		return false;
	}

	size_t left = 0;
	size_t right = track->instant_count;
	while (left < right) {
		size_t mid = left + (right - left) / 2;
		if (track->instant_events[mid].ts_ns <= ts_ns) {
			left = mid + 1;
		} else {
			right = mid;
		}
	}
	if (left >= track->instant_count) {
		return false;
	}
	*next_ts_ns = track->instant_events[left].ts_ns;
	return true;
}

bool nsi_perfetto_trace_get_next_counter_time(const char *track_name, uint64_t timestamp_ns,
					   uint64_t *next_timestamp_ns)
{
	ensure_loaded();
	if (next_timestamp_ns == NULL) {
		return false;
	}

	uint64_t trace_now = adjust_trace_timestamp(timestamp_ns);
	const struct nsi_perfetto_track *track = find_track_by_name(track_name);
	uint64_t next_int = UINT64_MAX;
	uint64_t next_double = UINT64_MAX;
	bool has_int = find_next_int(track, trace_now, &next_int);
	bool has_double = find_next_double(track, trace_now, &next_double);
	uint64_t next_trace = UINT64_MAX;

	if (has_int) {
		next_trace = next_int;
	}
	if (has_double && next_double < next_trace) {
		next_trace = next_double;
	}
	if (next_trace == UINT64_MAX) {
		return false;
	}

	if (trace_time_offset) {
		next_trace += trace_time_offset;
	}
	*next_timestamp_ns = next_trace;
	return true;
}

bool nsi_perfetto_trace_get_next_instant_time(const char *track_name, uint64_t timestamp_ns,
					      uint64_t *next_timestamp_ns)
{
	ensure_loaded();
	if (next_timestamp_ns == NULL) {
		return false;
	}

	uint64_t trace_now = adjust_trace_timestamp(timestamp_ns);
	const struct nsi_perfetto_track *track = find_track_by_name(track_name);
	uint64_t next_instant = UINT64_MAX;

	if (!find_next_instant(track, trace_now, &next_instant)) {
		return false;
	}

	if (trace_time_offset) {
		next_instant += trace_time_offset;
	}
	*next_timestamp_ns = next_instant;
	return true;
}

