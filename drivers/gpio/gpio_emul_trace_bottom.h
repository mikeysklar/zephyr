/*
 * Copyright (c) 2025 Scott Shawcroft for Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * "Bottom" of the GPIO emulator trace replay.
 * When built with the native_simulator this will be built in the runner context,
 * that is, with the host C library, and with the host include paths.
 */

#ifndef DRIVERS_GPIO_GPIO_EMUL_TRACE_BOTTOM_H
#define DRIVERS_GPIO_GPIO_EMUL_TRACE_BOTTOM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback for setting GPIO input values from the native simulator
 *
 * @param user_data Opaque user data (typically the Zephyr device pointer)
 * @param pin The pin number to update
 * @param value The new value (true for high, false for low)
 */
typedef void (*gpio_emul_trace_set_input_t)(void *user_data, uint8_t pin, bool value);

/**
 * @brief Data shared between Zephyr and native simulator for GPIO trace replay
 */
struct gpio_emul_trace_data {
	/** Callback to invoke when a GPIO input should change */
	gpio_emul_trace_set_input_t set_input;
	/** User data passed to callbacks */
	void *user_data;
};

/**
 * @brief Initialize the GPIO trace bottom layer
 *
 * Called from the Zephyr side to register the trace callbacks.
 *
 * @param data Pointer to the trace data structure (must remain valid)
 */
void gpio_emul_trace_init_bottom(struct gpio_emul_trace_data *data);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_GPIO_GPIO_EMUL_TRACE_BOTTOM_H */
