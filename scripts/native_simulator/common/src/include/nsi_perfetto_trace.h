/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NSI_PERFETTO_TRACE_H
#define NSI_PERFETTO_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void nsi_perfetto_trace_set_path(const char *path);
bool nsi_perfetto_trace_enabled(void);
bool nsi_perfetto_trace_get_counter_int(const char *track_name, uint64_t timestamp_ns,
					int64_t *value);
bool nsi_perfetto_trace_get_counter_double(const char *track_name, uint64_t timestamp_ns,
					   double *value);
bool nsi_perfetto_trace_get_next_counter_time(const char *track_name, uint64_t timestamp_ns,
					   uint64_t *next_timestamp_ns);
bool nsi_perfetto_trace_get_next_instant_time(const char *track_name, uint64_t timestamp_ns,
					      uint64_t *next_timestamp_ns);

#ifdef __cplusplus
}
#endif

#endif /* NSI_PERFETTO_TRACE_H */
