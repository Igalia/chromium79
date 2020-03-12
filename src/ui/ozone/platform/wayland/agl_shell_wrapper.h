#ifndef UI_OZONE_PLATFORM_WAYLAND_AGL_SHELL_WRAPPER_H_
#define UI_OZONE_PLATFORM_WAYLAND_AGL_SHELL_WRAPPER_H_

#include <cstdint>

#include "base/strings/string16.h"
#include "ui/ozone/platform/wayland/common/wayland_object.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"
#include "ui/ozone/platform/wayland/host/wayland_output.h"

#include "base/macros.h"

struct agl_shell;

namespace ui {

class WaylandConnection;
class WaylandWindow;

class AglShell {
public:
	AglShell(WaylandConnection *connection);
	~AglShell();

	void setPanel(WaylandWindow *window, WaylandOutput *output, uint32_t edge);
	void setBackGround(WaylandWindow *window, WaylandOutput *output);
	void ready(void);

	void activateApp(std::string app_id, WaylandOutput *output);
private:
	struct agl_shell *agl_shell;

	DISALLOW_COPY_AND_ASSIGN(AglShell);
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_WAYLAND_AGL_SHELL_WRAPPER_H_
