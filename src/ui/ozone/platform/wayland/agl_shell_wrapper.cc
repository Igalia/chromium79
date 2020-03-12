#include "ui/ozone/platform/wayland/agl_shell_wrapper.h"

#include <agl-shell-client-protocol.h>

#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_output_manager.h"
#include "ui/ozone/platform/wayland/common/wayland_util.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"
#include "ui/ozone/platform/wayland/host/wayland_output.h"

namespace ui {

AglShell::AglShell(WaylandConnection *wconnection)
{
	agl_shell = wconnection->ashell();
}

AglShell::~AglShell()
{
	agl_shell_destroy(agl_shell);
}

void
AglShell::setPanel(WaylandWindow *window, WaylandOutput *output, uint32_t edge)
{
	struct wl_surface *surface = window->surface();
	struct wl_output *woutput = output->output();

	agl_shell_set_panel(agl_shell, surface, woutput, edge);
}

void
AglShell::setBackGround(WaylandWindow *window, WaylandOutput *output)
{
	struct wl_surface *surface = window->surface();
	struct wl_output *woutput = output->output();

	agl_shell_set_background(agl_shell, surface, woutput);
}

void
AglShell::ready(void)
{
	agl_shell_ready(agl_shell);
}

void
AglShell::activateApp(std::string app_id, WaylandOutput *output)
{
	const char *_app_id = app_id.c_str();
	struct wl_output *woutput = output->output();

	agl_shell_activate_app(agl_shell, _app_id, woutput);
}

}  // namespace ui
