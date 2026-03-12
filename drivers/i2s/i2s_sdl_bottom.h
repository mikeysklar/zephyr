/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * "Bottom" of the SDL I2S driver.
 * When built with the native_simulator this will be built in the runner context,
 * that is, with the host C library, and with the host include paths.
 */
#ifndef DRIVERS_I2S_I2S_SDL_BOTTOM_H
#define DRIVERS_I2S_I2S_SDL_BOTTOM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct i2s_sdl_init_params {
	uint32_t sample_rate;
	uint8_t channels;
	uint8_t bits_per_sample;
	bool earless;
	/* Returns the opened SDL audio device ID (0 if earless). */
	uint32_t *audio_device_id;
};

/**
 * @brief Callback when a buffer's simulated playback time has elapsed.
 *
 * Called from NSI HW event context.  The top-layer implementation should
 * store the arguments and raise an IRQ so the actual work (slab free,
 * perfetto trace, semaphore give) happens in proper Zephyr ISR context.
 *
 * @param user_data   Opaque pointer registered with i2s_sdl_set_done_callback()
 * @param buffer      The buffer whose playback just finished
 * @param size        Buffer size in bytes
 * @param start_time_us  Simulated time (microseconds) when this buffer
 *                       started playing
 */
typedef void (*i2s_sdl_buffer_done_t)(void *user_data, void *buffer,
				      size_t size, uint64_t start_time_us);

/**
 * @brief Initialize SDL audio device.
 * @return 0 on success, -1 on failure.
 */
int i2s_sdl_init_bottom(struct i2s_sdl_init_params *params);

/**
 * @brief Register buffer-completion callback.
 *
 * Must be called before i2s_sdl_start_bottom().
 */
void i2s_sdl_set_done_callback(i2s_sdl_buffer_done_t cb, void *user_data);

/**
 * @brief Submit a buffer for timed playback.
 *
 * The buffer is queued internally.  When the NSI HW event fires at the
 * correct simulated time the buffer is sent to SDL and the completion
 * callback is invoked.
 *
 * @param audio_device_id  SDL audio device (0 = earless)
 * @param buffer           Audio data
 * @param size             Size in bytes
 * @param duration_us      How long this buffer takes to play (microseconds)
 */
void i2s_sdl_submit_buffer(uint32_t audio_device_id, void *buffer,
			   size_t size, uint32_t duration_us);

/**
 * @brief Start timed playback of queued buffers.
 *
 * Begins the NSI HW event chain.  Also unpauses the SDL audio device.
 */
void i2s_sdl_start_bottom(uint32_t audio_device_id);

/**
 * @brief Pause SDL audio playback.
 */
void i2s_sdl_pause_bottom(uint32_t audio_device_id);

/**
 * @brief Resume (unpause) SDL audio playback.
 */
void i2s_sdl_unpause_bottom(uint32_t audio_device_id);

/**
 * @brief Stop playback: cancel pending NSI events and clear SDL queue.
 */
void i2s_sdl_stop_bottom(uint32_t audio_device_id);

/**
 * @brief Discard all queued but unplayed buffers, invoking the done
 *        callback for each so the top layer can free their slabs.
 */
void i2s_sdl_drop_buffers(void);

/**
 * @brief Set WAV capture file path.  If non-NULL, all audio data
 *        sent to SDL will also be written to this WAV file.
 */
void i2s_sdl_set_capture_path(const char *path);

/**
 * @brief Close SDL audio device.
 */
void i2s_sdl_cleanup_bottom(uint32_t audio_device_id);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_I2S_I2S_SDL_BOTTOM_H */
