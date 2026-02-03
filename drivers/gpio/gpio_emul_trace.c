/*
 * Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO emulator trace replay for native_sim - runs in the native simulator.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "nsi_hws_models_if.h"
#include "nsi_hw_scheduler.h"
#include "nsi_perfetto_trace.h"
#include "nsi_tasks.h"

#include "gpio_emul_trace_bottom.h"

static struct gpio_emul_trace_data *trace_data;
static uint64_t gpio_emul_trace_event_time = NSI_NEVER;

void gpio_emul_trace_init_bottom(struct gpio_emul_trace_data *data)
{
	trace_data = data;
	if (nsi_perfetto_trace_enabled()) {
		gpio_emul_trace_event_time = nsi_hws_get_time();
		nsi_hws_find_next_event();
	}
}

static void gpio_emul_trace_event(void)
{
	if (!trace_data || !trace_data->set_input || !nsi_perfetto_trace_enabled()) {
		gpio_emul_trace_event_time = NSI_NEVER;
		return;
	}

	uint64_t now_us = nsi_hws_get_time();
	uint64_t now_ns = now_us * 1000U;
	uint64_t next_event_ns = UINT64_MAX;

	/* Check each pin for trace events */
	for (uint8_t pin = 0; pin < 32; pin++) {
		char track_name[32];
		int64_t value = 0;
		uint64_t pin_next = 0;

		int len = snprintf(track_name, sizeof(track_name), "gpio_emul.%02u", pin);
		if (len <= 0 || len >= (int)sizeof(track_name)) {
			continue;
		}

		if (nsi_perfetto_trace_get_counter_int(track_name, now_ns, &value)) {
			if (trace_data->set_input) {
				trace_data->set_input(trace_data->user_data, pin, value != 0);
			}
		}

		if (nsi_perfetto_trace_get_next_counter_time(track_name, now_ns, &pin_next)) {
			if (pin_next < next_event_ns) {
				next_event_ns = pin_next;
			}
		}
	}

	if (next_event_ns == UINT64_MAX) {
		gpio_emul_trace_event_time = NSI_NEVER;
	} else {
		uint64_t next_event_us = next_event_ns / 1000U;
		if (next_event_us <= now_us) {
			next_event_us = now_us + 1;
		}
		gpio_emul_trace_event_time = next_event_us;
	}
}

NSI_HW_EVENT(gpio_emul_trace_event_time, gpio_emul_trace_event, 100);
