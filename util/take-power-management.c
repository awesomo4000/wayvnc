/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this software, either in source code form or as a compiled binary, for any
 * purpose, commercial or non-commercial, and by any means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors of this
 * software dedicate any and all copyright interest in the software to the
 * public domain. We make this dedication for the benefit of the public at large
 * and to the detriment of our heirs and successors. We intend this dedication
 * to be an overt act of relinquishment in perpetuity of all present and future
 * rights to this software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <https://unlicense.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "wlr-output-power-management-unstable-v1.h"

struct state {
	struct zwlr_output_power_manager_v1* power_manager;
	struct wl_output* output;
	struct zwlr_output_power_v1* output_power;
};

static void handle_power_mode(void* data,
		struct zwlr_output_power_v1* output_power, uint32_t mode)
{
	fprintf(stderr, "Output power mode is now %s\n",
			mode == ZWLR_OUTPUT_POWER_V1_MODE_ON ? "on" : "off");
}

static void handle_power_failed(void* data,
		struct zwlr_output_power_v1* output_power)
{
	fprintf(stderr, "Output power management control was revoked\n");
}

static const struct zwlr_output_power_v1_listener power_listener = {
	.mode = handle_power_mode,
	.failed = handle_power_failed,
};

static void registry_add(void* data, struct wl_registry* registry, uint32_t id,
		const char* interface, uint32_t version)
{
	struct state* self = data;

	if (strcmp(interface, zwlr_output_power_manager_v1_interface.name) == 0) {
		self->power_manager = wl_registry_bind(registry, id,
				&zwlr_output_power_manager_v1_interface, 1);
	} else if (!self->output &&
			strcmp(interface, wl_output_interface.name) == 0) {
		self->output = wl_registry_bind(registry, id,
				&wl_output_interface, 1);
	}
}

static void registry_remove(void* data, struct wl_registry* registry,
		uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_add,
	.global_remove = registry_remove,
};

int main(int argc, char* argv[])
{
	struct wl_display* display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "Failed to connect to the compositor\n");
		return 1;
	}

	struct state state = { 0 };

	struct wl_registry* registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &state);
	wl_display_roundtrip(display);

	if (!state.power_manager) {
		fprintf(stderr,
			"Compositor does not support output power management\n");
		goto failure;
	}

	if (!state.output) {
		fprintf(stderr, "Compositor has no outputs\n");
		goto failure;
	}

	state.output_power = zwlr_output_power_manager_v1_get_output_power(
			state.power_manager, state.output);
	zwlr_output_power_v1_add_listener(state.output_power, &power_listener,
			&state);

	fprintf(stderr, "Holding output power management\n");

	while (wl_display_dispatch(display) >= 0);

failure:
	if (state.output_power)
		zwlr_output_power_v1_destroy(state.output_power);
	if (state.power_manager)
		zwlr_output_power_manager_v1_destroy(state.power_manager);
	if (state.output)
		wl_output_destroy(state.output);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return state.output_power ? 0 : 1;
}
