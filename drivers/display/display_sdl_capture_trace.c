/*
 * Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SDL display capture driven by Perfetto instant events - runs in native sim context.
 *
 * Because the SDL renderer is owned by the Zephyr SDL task thread, we cannot
 * call SDL_RenderReadPixels from the native_sim HW-event context.  Instead,
 * sdl_display_capture_trace_update() is called from the SDL task thread after
 * every present to snapshot the frame into a shared buffer.  The HW-event
 * callback then writes that buffer to a PNG file at the requested time.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_image.h>
#include "nsi_hws_models_if.h"
#include "nsi_hw_scheduler.h"
#include "nsi_perfetto_trace.h"
#include "nsi_tasks.h"
#include "nsi_tracing.h"

static const char *capture_png_path;
static int capture_sequence_number;
static uint64_t capture_event_time = NSI_NEVER;

/* Last-frame snapshot (written by SDL thread, read by HW-event thread). */
static uint8_t *frame_pixels;
static int frame_width;
static int frame_height;
static bool frame_valid;

#define DISPLAY_CAPTURE_TRACK_NAME "display_capture"

static void write_png_from_buffer(const char *path)
{
	if (!frame_valid || !frame_pixels) {
		return;
	}

	SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(
		frame_pixels, frame_width, frame_height, 32, frame_width * 4,
		0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);

	if (surface == NULL) {
		nsi_print_warning("Failed to create SDL surface for capture: %s", SDL_GetError());
		return;
	}

	if (IMG_SavePNG(surface, path) != 0) {
		nsi_print_warning("Failed to save PNG capture %s: %s", path, IMG_GetError());
	}

	SDL_FreeSurface(surface);
}

static void schedule_next_event(uint64_t now_us)
{
	uint64_t now_ns = now_us * 1000U;
	uint64_t next_ns = 0;

	if (nsi_perfetto_trace_get_next_instant_time(DISPLAY_CAPTURE_TRACK_NAME, now_ns,
						     &next_ns)) {
		uint64_t next_us = next_ns / 1000U;

		if (next_us <= now_us) {
			next_us = now_us + 1;
		}
		capture_event_time = next_us;
	} else {
		capture_event_time = NSI_NEVER;
	}
}

static void capture_event_callback(void)
{
	if (!capture_png_path || !nsi_perfetto_trace_enabled()) {
		capture_event_time = NSI_NEVER;
		return;
	}

	char path[512];

	snprintf(path, sizeof(path), capture_png_path, capture_sequence_number++);
	write_png_from_buffer(path);

	schedule_next_event(nsi_hws_get_time());
}

NSI_HW_EVENT(capture_event_time, capture_event_callback, 100);

void sdl_display_capture_trace_update(void *renderer)
{
	if (!capture_png_path || capture_event_time == NSI_NEVER) {
		return;
	}

	int width = 0;
	int height = 0;

	SDL_RenderGetLogicalSize(renderer, &width, &height);
	if (width <= 0 || height <= 0) {
		if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0 ||
		    width <= 0 || height <= 0) {
			return;
		}
	}

	size_t needed = (size_t)width * (size_t)height * 4;

	if (!frame_pixels || frame_width != width || frame_height != height) {
		free(frame_pixels);
		frame_pixels = malloc(needed);
		if (!frame_pixels) {
			frame_valid = false;
			return;
		}
	}

	if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
				 frame_pixels, width * 4) != 0) {
		frame_valid = false;
		return;
	}

	frame_width = width;
	frame_height = height;
	frame_valid = true;
}

void sdl_display_capture_trace_init(const char *png_path)
{
	capture_png_path = png_path;

	if (png_path == NULL || png_path[0] == '\0') {
		return;
	}
	if (!nsi_perfetto_trace_enabled()) {
		return;
	}

	schedule_next_event(nsi_hws_get_time());
	nsi_hws_find_next_event();
}
