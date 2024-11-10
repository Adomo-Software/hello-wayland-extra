#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <linux/input-event-codes.h>
#include <cairo/cairo.h>
#include <wchar.h>

#include "shm.h"
#include "xdg-shell-client-protocol.h"
#ifdef USE_XDG_DECORATION
#include "xdg-decoration-unstable-v1-client-protocol.h"
#endif

static int32_t width = 1000;
static int32_t height = 400;

static bool configured = false;
static bool running = true;

static struct wl_shm *shm = NULL;
static struct wl_compositor *compositor = NULL;
static struct xdg_wm_base *xdg_wm_base = NULL;

static void *shm_data = NULL;
static struct wl_surface *surface = NULL;
static struct xdg_toplevel *xdg_toplevel = NULL;
static struct wl_buffer *buffer = NULL;
#ifdef USE_XDG_DECORATION
struct zxdg_decoration_manager_v1 *decoration_manager;
#endif

static void noop() {
	// This space intentionally left blank
}

static void xdg_toplevel_handle_configure(void *data,
		struct xdg_toplevel *xdg_toplevel, int32_t new_width, int32_t new_height,
																					struct wl_array *states);

static void xdg_wm_base_handle_ping(void *data,
		struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	// The compositor will send us a ping event to check that we're responsive.
	// We need to send back a pong request immediately.
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_handle_ping,
};

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	// The compositor configures our surface, acknowledge the configure event
	xdg_surface_ack_configure(xdg_surface, serial);

	if (configured) {
		// If this isn't the first configure event we've received, we already
		// have a buffer attached, so no need to do anything. Commit the
		// surface to apply the configure acknowledgement.
		wl_surface_commit(surface);
	}

	configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};


static void xdg_toplevel_handle_close(void *data,
		struct xdg_toplevel *xdg_toplevel) {
	// Stop running if the user requests to close the toplevel
	running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

static void pointer_handle_button(void *data, struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct wl_seat *seat = data;
	if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		xdg_toplevel_move(xdg_toplevel, seat, serial);
	}
}


static const struct wl_pointer_listener pointer_listener = {
	.enter = noop,
	.leave = noop,
	.motion = noop,
	.button = pointer_handle_button,
	.axis = noop,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	// If the wl_seat has the pointer capability, start listening to pointer
	// events
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		struct wl_pointer *pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, seat);
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct wl_seat *seat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(xdg_wm_base, &xdg_wm_base_listener, NULL);
	}
#ifdef USE_XDG_DECORATION
	else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
		decoration_manager = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
	}
#endif
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// Who cares
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static void set_source_u32(cairo_t *cairo, uint32_t color) {
  cairo_set_source_rgba(cairo,
												(color >> (0 * 8) & 0xFF) / 255.0,
                        (color >> (1 * 8) & 0xFF) / 255.0,
                        (color >> (2 * 8) & 0xFF) / 255.0,
                        (color >> (3 * 8) & 0xFF) / 255.0);
}


static struct wl_buffer *create_buffer(void) {
	int stride = width * 4;
	int size = stride * height;

	// Allocate a shared memory file with the right size
	int fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
		return NULL;
	}

	// Map the shared memory file
	shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (shm_data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	// Create a wl_buffer from our shared memory file descriptor
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
		stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);

	// Now that we've mapped the file and created the wl_buffer, we no longer
	// need to keep file descriptor opened
	close(fd);

	/*                        A.R.G.B. */
	uint32_t foreground = L'\xFF000000';
	uint32_t background = L'\xFFFFFFFF';
	const char* text = "Hi, mom";
	double font_size = 200;
	double font_scale = 1.0;

	wmemset(shm_data, background, width * height);
	
	// Text rendering stuff
	const cairo_format_t cairo_fmt = CAIRO_FORMAT_ARGB32;

	cairo_surface_t* cairo_surface = cairo_image_surface_create_for_data(shm_data, cairo_fmt, width, height, stride);
	cairo_t *cairo = cairo_create(cairo_surface);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);

	set_source_u32(cairo, foreground);
	cairo_select_font_face(cairo, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

	cairo_set_font_size(cairo, font_size * font_scale);
	cairo_text_extents_t extents;
	cairo_text_extents(cairo, text, &extents);
  cairo_move_to(cairo,
								((width / 2) - (extents.width / 2 + extents.x_bearing)) * font_scale,
								((height / 2) - (extents.height / 2 + extents.y_bearing)) * font_scale);
  cairo_show_text(cairo, text);

	return buffer;
}

static void xdg_toplevel_handle_configure(void *data,
		struct xdg_toplevel *xdg_toplevel, int32_t new_width, int32_t new_height,
		struct wl_array *states) {
	if (new_width > 0 && new_height > 0) {
		width = new_width;
		height = new_height;
		if (buffer) {
			wl_buffer_destroy(buffer);
			buffer = create_buffer();
			wl_surface_attach(surface, buffer, 0, 0);
			wl_surface_commit(surface);
		}
	}
}

int main(int argc, char *argv[]) {
	// Connect to the Wayland compositor
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return EXIT_FAILURE;
	}

	// Obtain the wl_registry and fetch the list of globals
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	if (wl_display_roundtrip(display) == -1) {
		return EXIT_FAILURE;
	}

	// Check that all globals we require are available
	if (shm == NULL || compositor == NULL || xdg_wm_base == NULL) {
		fprintf(stderr, "no wl_shm, wl_compositor or xdg_wm_base support\n");
		return EXIT_FAILURE;
	}

	// Create a wl_surface, a xdg_surface and a xdg_toplevel
	surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

#ifdef USE_XDG_DECORATION
	if (decoration_manager) {
		struct zxdg_toplevel_decoration_v1 *decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(decoration_manager, xdg_toplevel);
		zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
#endif
		
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	// Perform the initial commit and wait for the first configure event
	wl_surface_commit(surface);

	while (wl_display_dispatch(display) != -1 && !configured) {
		// This space intentionally left blank
	}

	// Create a wl_buffer, attach it to the surface and commit the surface
	buffer = create_buffer();
	if (buffer == NULL) {
		return EXIT_FAILURE;
	}

	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_commit(surface);

	// Continue dispatching events until the user closes the toplevel
	while (wl_display_dispatch(display) != -1 && running) {
		// This space intentionally left blank
	}

	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);
	wl_buffer_destroy(buffer);

	return EXIT_SUCCESS;
}
