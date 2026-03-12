/*
 * Copyright (c) 2026 Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * "Bottom" of the SDL I2S driver — runs in native simulator (host) context.
 *
 * Uses NSI hardware event scheduling for exact simulated-time buffer
 * completion, avoiding the tick-granularity issues of k_usleep().
 *
 * SDL audio uses callback mode with a ring buffer so playback is
 * smooth in -rt mode: the NSI events write into the ring buffer at
 * simulated time, and SDL's audio thread drains it at real-time rate.
 */

#include "i2s_sdl_bottom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "nsi_hws_models_if.h"
#include "nsi_hw_scheduler.h"
#include "nsi_tracing.h"

/* ------------------------------------------------------------------ */
/* WAV capture                                                         */
/* ------------------------------------------------------------------ */

static const char *i2s_capture_path;
static FILE *wav_file;
static uint32_t wav_data_bytes;
static uint32_t wav_sample_rate;
static uint16_t wav_channels;
static uint16_t wav_bits_per_sample;

static void wav_write_header(void)
{
	if (!wav_file) {
		return;
	}

	uint16_t block_align = wav_channels * (wav_bits_per_sample / 8);
	uint32_t byte_rate = wav_sample_rate * block_align;

	fwrite("RIFF", 1, 4, wav_file);
	uint32_t riff_size = 36 + wav_data_bytes;

	fwrite(&riff_size, 4, 1, wav_file);
	fwrite("WAVE", 1, 4, wav_file);

	fwrite("fmt ", 1, 4, wav_file);
	uint32_t fmt_size = 16;

	fwrite(&fmt_size, 4, 1, wav_file);
	uint16_t audio_format = 1; /* PCM */

	fwrite(&audio_format, 2, 1, wav_file);
	fwrite(&wav_channels, 2, 1, wav_file);
	fwrite(&wav_sample_rate, 4, 1, wav_file);
	fwrite(&byte_rate, 4, 1, wav_file);
	fwrite(&block_align, 2, 1, wav_file);
	fwrite(&wav_bits_per_sample, 2, 1, wav_file);

	fwrite("data", 1, 4, wav_file);
	fwrite(&wav_data_bytes, 4, 1, wav_file);
}

static void wav_close(void);

static void wav_atexit(void)
{
	wav_close();
}

static void wav_open(uint32_t sample_rate, uint8_t channels,
		     uint8_t bits_per_sample)
{
	if (!i2s_capture_path) {
		return;
	}

	wav_file = fopen(i2s_capture_path, "wb");
	if (!wav_file) {
		nsi_print_warning("I2S: cannot open capture file %s",
				  i2s_capture_path);
		return;
	}

	wav_sample_rate = sample_rate;
	wav_channels = channels;
	wav_bits_per_sample = bits_per_sample;
	wav_data_bytes = 0;

	wav_write_header();
	atexit(wav_atexit);
}

static void wav_append(const void *buf, size_t size)
{
	if (!wav_file) {
		return;
	}
	fwrite(buf, 1, size, wav_file);
	wav_data_bytes += size;
}

static void wav_close(void)
{
	if (!wav_file) {
		return;
	}

	fseek(wav_file, 0, SEEK_SET);
	wav_write_header();
	fclose(wav_file);
	wav_file = NULL;
}

/* ------------------------------------------------------------------ */
/* SDL audio ring buffer (lock-free single-producer single-consumer)   */
/* ------------------------------------------------------------------ */

/* 64 KB ring — enough for ~1 second at 16-bit stereo 16 kHz */
#define RING_SIZE (64 * 1024)

static uint8_t ring_buf[RING_SIZE];
static volatile size_t ring_write; /* written by NSI event (producer) */
static volatile size_t ring_read;  /* written by SDL callback (consumer) */

static size_t ring_available(void)
{
	size_t w = ring_write;
	size_t r = ring_read;

	return (w >= r) ? (w - r) : (RING_SIZE - r + w);
}

static void ring_push(const void *data, size_t len)
{
	const uint8_t *src = (const uint8_t *)data;
	size_t w = ring_write;

	for (size_t i = 0; i < len; i++) {
		ring_buf[w] = src[i];
		w = (w + 1) % RING_SIZE;
	}
	ring_write = w;
}

static void ring_pop(void *out, size_t len)
{
	uint8_t *dst = (uint8_t *)out;
	size_t r = ring_read;

	for (size_t i = 0; i < len; i++) {
		dst[i] = ring_buf[r];
		r = (r + 1) % RING_SIZE;
	}
	ring_read = r;
}

static void ring_clear(void)
{
	ring_write = 0;
	ring_read = 0;
}

/* SDL audio callback — runs on SDL's audio thread */
static void sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
	(void)userdata;
	size_t avail = ring_available();

	if (avail >= (size_t)len) {
		ring_pop(stream, (size_t)len);
	} else {
		/* Partial data + silence for the rest */
		if (avail > 0) {
			ring_pop(stream, avail);
		}
		memset(stream + avail, 0, (size_t)len - avail);
	}
}

/* ------------------------------------------------------------------ */
/* Internal buffer queue (for NSI event timing)                        */
/* ------------------------------------------------------------------ */

#define I2S_BUF_QUEUE_LEN 8

struct i2s_buf_entry {
	void *buffer;
	size_t size;
	uint32_t duration_us;
	uint32_t audio_device_id;
};

static struct i2s_buf_entry buf_queue[I2S_BUF_QUEUE_LEN];
static int bq_head;
static int bq_tail;
static int bq_count;

static i2s_sdl_buffer_done_t done_cb;
static void *done_cb_user_data;

static struct i2s_buf_entry current_buf;
static uint64_t current_buf_start_us;
static bool current_buf_valid;

static uint64_t playback_clock_us;
static bool playback_active;

static uint64_t i2s_sdl_event_time = NSI_NEVER;

/* ------------------------------------------------------------------ */
/* Queue helpers                                                       */
/* ------------------------------------------------------------------ */

static bool bq_empty(void)
{
	return bq_count == 0;
}

static bool bq_full(void)
{
	return bq_count >= I2S_BUF_QUEUE_LEN;
}

static void bq_push(const struct i2s_buf_entry *e)
{
	buf_queue[bq_tail] = *e;
	bq_tail = (bq_tail + 1) % I2S_BUF_QUEUE_LEN;
	bq_count++;
}

static struct i2s_buf_entry *bq_peek(void)
{
	if (bq_empty()) {
		return NULL;
	}
	return &buf_queue[bq_head];
}

static void bq_pop(void)
{
	bq_head = (bq_head + 1) % I2S_BUF_QUEUE_LEN;
	bq_count--;
}

static void bq_clear(void)
{
	bq_head = bq_tail = bq_count = 0;
}

/* ------------------------------------------------------------------ */
/* Start the next buffer from the queue                                */
/* ------------------------------------------------------------------ */

static void start_next_buffer(void)
{
	struct i2s_buf_entry *e = bq_peek();

	if (e == NULL) {
		i2s_sdl_event_time = NSI_NEVER;
		current_buf_valid = false;
		return;
	}

	uint64_t now_us = nsi_hws_get_time();

	if (playback_clock_us < now_us) {
		playback_clock_us = now_us;
	}

	current_buf = *e;
	current_buf_start_us = playback_clock_us;
	current_buf_valid = true;
	bq_pop();

	/* Push audio data into the ring buffer for SDL's callback */
	if (current_buf.audio_device_id != 0) {
		ring_push(current_buf.buffer, current_buf.size);
	}

	/* Capture to WAV */
	wav_append(current_buf.buffer, current_buf.size);

	/* Schedule the NSI event for when this buffer finishes */
	playback_clock_us += current_buf.duration_us;
	i2s_sdl_event_time = playback_clock_us;
}

/* ------------------------------------------------------------------ */
/* NSI HW event callback                                               */
/* ------------------------------------------------------------------ */

static void i2s_sdl_hw_event(void)
{
	if (!playback_active) {
		i2s_sdl_event_time = NSI_NEVER;
		return;
	}

	if (current_buf_valid && done_cb) {
		done_cb(done_cb_user_data, current_buf.buffer,
			current_buf.size, current_buf_start_us);
		current_buf_valid = false;
	}

	start_next_buffer();
}

NSI_HW_EVENT(i2s_sdl_event_time, i2s_sdl_hw_event, 100);

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int i2s_sdl_init_bottom(struct i2s_sdl_init_params *params)
{
	if (params->earless) {
		*params->audio_device_id = 0;
		wav_open(params->sample_rate, params->channels,
			 params->bits_per_sample);
		return 0;
	}

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		nsi_print_warning("Failed to init SDL audio: %s",
				  SDL_GetError());
		return -1;
	}

	SDL_AudioSpec desired, obtained;

	SDL_memset(&desired, 0, sizeof(desired));
	desired.freq = (int)params->sample_rate;
	desired.channels = params->channels;
	desired.samples = 512;
	desired.callback = sdl_audio_callback;
	desired.userdata = NULL;

	switch (params->bits_per_sample) {
	case 8:
		desired.format = AUDIO_S8;
		break;
	case 16:
		desired.format = AUDIO_S16LSB;
		break;
	case 32:
		desired.format = AUDIO_S32LSB;
		break;
	default:
		desired.format = AUDIO_S16LSB;
		break;
	}

	ring_clear();

	SDL_AudioDeviceID dev =
		SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
	if (dev == 0) {
		nsi_print_warning("Failed to open SDL audio device: %s",
				  SDL_GetError());
		return -1;
	}

	*params->audio_device_id = (uint32_t)dev;

	wav_open(params->sample_rate, params->channels,
		 params->bits_per_sample);

	return 0;
}

void i2s_sdl_set_done_callback(i2s_sdl_buffer_done_t cb, void *user_data)
{
	done_cb = cb;
	done_cb_user_data = user_data;
}

void i2s_sdl_submit_buffer(uint32_t audio_device_id, void *buffer,
			   size_t size, uint32_t duration_us)
{
	if (bq_full()) {
		nsi_print_warning("I2S SDL: buffer queue full, dropping");
		return;
	}

	struct i2s_buf_entry entry = {
		.buffer = buffer,
		.size = size,
		.duration_us = duration_us,
		.audio_device_id = audio_device_id,
	};
	bq_push(&entry);

	if (playback_active && !current_buf_valid) {
		start_next_buffer();
		nsi_hws_find_next_event();
	}
}

void i2s_sdl_start_bottom(uint32_t audio_device_id)
{
	playback_active = true;
	playback_clock_us = nsi_hws_get_time();
	current_buf_valid = false;

	if (audio_device_id != 0) {
		SDL_PauseAudioDevice((SDL_AudioDeviceID)audio_device_id, 0);
	}

	if (!bq_empty()) {
		start_next_buffer();
		nsi_hws_find_next_event();
	}
}

void i2s_sdl_pause_bottom(uint32_t audio_device_id)
{
	if (audio_device_id != 0) {
		SDL_PauseAudioDevice((SDL_AudioDeviceID)audio_device_id, 1);
	}
	playback_active = false;
	i2s_sdl_event_time = NSI_NEVER;
}

void i2s_sdl_unpause_bottom(uint32_t audio_device_id)
{
	if (audio_device_id != 0) {
		SDL_PauseAudioDevice((SDL_AudioDeviceID)audio_device_id, 0);
	}
}

void i2s_sdl_stop_bottom(uint32_t audio_device_id)
{
	playback_active = false;
	i2s_sdl_event_time = NSI_NEVER;
	current_buf_valid = false;

	if (audio_device_id != 0) {
		SDL_PauseAudioDevice((SDL_AudioDeviceID)audio_device_id, 1);
		ring_clear();
	}
}

void i2s_sdl_drop_buffers(void)
{
	if (current_buf_valid && done_cb) {
		done_cb(done_cb_user_data, current_buf.buffer,
			current_buf.size, 0);
		current_buf_valid = false;
	}

	while (!bq_empty()) {
		struct i2s_buf_entry *e = bq_peek();

		if (done_cb) {
			done_cb(done_cb_user_data, e->buffer, e->size, 0);
		}
		bq_pop();
	}
}

void i2s_sdl_cleanup_bottom(uint32_t audio_device_id)
{
	i2s_sdl_stop_bottom(audio_device_id);
	bq_clear();
	wav_close();

	if (audio_device_id != 0) {
		SDL_CloseAudioDevice((SDL_AudioDeviceID)audio_device_id);
	}
}

void i2s_sdl_set_capture_path(const char *path)
{
	i2s_capture_path = path;
}
