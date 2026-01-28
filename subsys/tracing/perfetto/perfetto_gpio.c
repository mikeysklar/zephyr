/*
 * Copyright (c) 2024 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

#include "perfetto_encoder.h"
#include "perfetto_gpio.h"

/*
 * Generate GPIO pin track information from device tree.
 * Each GPIO port gets tracks for each of its pins (up to ngpios).
 */

/* Structure to hold GPIO port track info */
struct gpio_port_track_info {
	const struct device *dev;
	const char *name;
	uint8_t ngpios;
	uint64_t track_uuid_base;
};

/* Forward declarations for DT macros */
#define GPIO_PORT_TRACK_INFO(node_id) \
	{ \
		.dev = DEVICE_DT_GET(node_id), \
		.name = DT_NODE_FULL_NAME(node_id), \
		.ngpios = DT_PROP_OR(node_id, ngpios, 32), \
		.track_uuid_base = (DT_DEP_ORD(node_id) << 8), \
	},

#define GPIO_PORT_NODE_IS_GPIO_CTLR(node_id)			\
		DT_PROP_OR(node_id, gpio_controller, 0)

#define GPIO_PORT_COND_TRACK_INFO(node_id)				\
	IF_ENABLED(GPIO_PORT_NODE_IS_GPIO_CTLR(node_id),	\
		   (GPIO_PORT_TRACK_INFO(node_id)))

/* Build array of GPIO port track info from device tree */
static const struct gpio_port_track_info gpio_ports[] = {
	DT_FOREACH_STATUS_OKAY_NODE(GPIO_PORT_COND_TRACK_INFO)
};

#define NUM_GPIO_PORTS ARRAY_SIZE(gpio_ports)

/* Track the last known state for each port to detect changes */
static gpio_port_value_t gpio_last_state[NUM_GPIO_PORTS];
static bool gpio_tracks_initialized;

/**
 * @brief Get the track UUID for a GPIO port group
 *
 * @param port_idx Index into gpio_ports array
 * @return Track UUID for this GPIO port group
 */
static inline uint64_t gpio_port_track_uuid(size_t port_idx)
{
	/* Use base + 256 for port group (pins use 0-255) */
	return gpio_ports[port_idx].track_uuid_base + 256;
}

/**
 * @brief Get the track UUID for a specific GPIO pin
 *
 * @param port_idx Index into gpio_ports array
 * @param pin Pin number within the port
 * @return Track UUID for this GPIO pin
 */
static inline uint64_t gpio_pin_track_uuid(size_t port_idx, uint8_t pin)
{
	return gpio_ports[port_idx].track_uuid_base + pin;
}

/**
 * @brief Find the port index for a given device
 *
 * @param port Device pointer
 * @return Port index, or -1 if not found
 */
static int find_port_index(const struct device *port)
{
	for (size_t i = 0; i < NUM_GPIO_PORTS; i++) {
		if (gpio_ports[i].dev == port) {
			return (int)i;
		}
	}
	return -1;
}

/**
 * @brief Emit counter updates for changed GPIO pins
 *
 * @param port_idx Port index
 * @param old_state Previous GPIO state
 * @param new_state New GPIO state
 */
static void emit_gpio_changes(int port_idx, gpio_port_value_t old_state,
			      gpio_port_value_t new_state)
{
	gpio_port_value_t changed = old_state ^ new_state;
	uint8_t ngpios = gpio_ports[port_idx].ngpios;

	for (uint8_t pin = 0; pin < ngpios && changed != 0; pin++) {
		if (changed & BIT(pin)) {
			uint64_t track_uuid = gpio_pin_track_uuid(port_idx, pin);
			int64_t value = (new_state & BIT(pin)) ? 1 : 0;

			perfetto_emit_counter(track_uuid, value);
			changed &= ~BIT(pin);
		}
	}
}

/**
 * @brief Initialize GPIO counter tracks
 *
 * Creates counter track descriptors for each GPIO pin based on device tree.
 * Called from perfetto_start().
 */
void perfetto_gpio_init_tracks(void)
{
	char track_name[32];
	uint64_t trace_uuid = perfetto_get_trace_uuid();

	if (gpio_tracks_initialized) {
		return;
	}

	for (size_t port_idx = 0; port_idx < NUM_GPIO_PORTS; port_idx++) {
		const struct gpio_port_track_info *info = &gpio_ports[port_idx];
		uint64_t port_uuid = gpio_port_track_uuid(port_idx);

		/* Create a parent track for this GPIO port under the Trace track */
		perfetto_emit_track_descriptor(port_uuid, trace_uuid, info->name);

		/* Create a counter track for each pin in this port */
		for (uint8_t pin = 0; pin < info->ngpios; pin++) {
			uint64_t track_uuid = gpio_pin_track_uuid(port_idx, pin);

			snprintf(track_name, sizeof(track_name), "%s.%02u",
				 info->name, pin);

			perfetto_emit_counter_track_descriptor(track_uuid,
							       port_uuid,
							       track_name);
			perfetto_emit_counter(track_uuid, 0);
		}

		/* Initialize last state to 0 */
		gpio_last_state[port_idx] = 0;
	}

	gpio_tracks_initialized = true;
}

/* GPIO */


void perfetto_trace_gpio_port_get_raw_enter(const struct device *port, gpio_port_value_t *value)
{
}

void perfetto_trace_gpio_port_get_raw_exit(const struct device *port, int ret)
{
}

void perfetto_trace_gpio_port_set_masked_raw_enter(const struct device *port, gpio_port_pins_t mask,
						   gpio_port_value_t value)
{
	int port_idx = find_port_index(port);

	if (port_idx < 0 || !gpio_tracks_initialized) {
		return;
	}

	gpio_port_value_t old_state = gpio_last_state[port_idx];
	gpio_port_value_t new_state = (old_state & ~mask) | (value & mask);

	if (old_state != new_state) {
		emit_gpio_changes(port_idx, old_state, new_state);
		gpio_last_state[port_idx] = new_state;
	}
}

void perfetto_trace_gpio_port_set_masked_raw_exit(const struct device *port, int ret)
{
}

void perfetto_trace_gpio_port_set_bits_raw_enter(const struct device *port, gpio_port_pins_t pins)
{
	int port_idx = find_port_index(port);

	if (port_idx < 0 || !gpio_tracks_initialized) {
		return;
	}

	gpio_port_value_t old_state = gpio_last_state[port_idx];
	gpio_port_value_t new_state = old_state | pins;

	if (old_state != new_state) {
		emit_gpio_changes(port_idx, old_state, new_state);
		gpio_last_state[port_idx] = new_state;
	}
}

void perfetto_trace_gpio_port_set_bits_raw_exit(const struct device *port, int ret)
{
}

void perfetto_trace_gpio_port_clear_bits_raw_enter(const struct device *port, gpio_port_pins_t pins)
{
	int port_idx = find_port_index(port);

	if (port_idx < 0 || !gpio_tracks_initialized) {
		return;
	}

	gpio_port_value_t old_state = gpio_last_state[port_idx];
	gpio_port_value_t new_state = old_state & ~pins;

	if (old_state != new_state) {
		emit_gpio_changes(port_idx, old_state, new_state);
		gpio_last_state[port_idx] = new_state;
	}
}

void perfetto_trace_gpio_port_clear_bits_raw_exit(const struct device *port, int ret)
{
}

void perfetto_trace_gpio_port_toggle_bits_enter(const struct device *port, gpio_port_pins_t pins)
{
	int port_idx = find_port_index(port);

	if (port_idx < 0 || !gpio_tracks_initialized) {
		return;
	}

	gpio_port_value_t old_state = gpio_last_state[port_idx];
	gpio_port_value_t new_state = old_state ^ pins;

	if (old_state != new_state) {
		emit_gpio_changes(port_idx, old_state, new_state);
		gpio_last_state[port_idx] = new_state;
	}
}

void perfetto_trace_gpio_port_toggle_bits_exit(const struct device *port, int ret)
{
}
