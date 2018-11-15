// Copyright 2014 Intel Corporation. All rights reserved.
// Copyright 2014-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <sys/types.h>
#include <unistd.h>

#include "ozone/wayland/shell/ivi_shell_surface.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"

#include "ozone/wayland/display.h"
#include "ozone/wayland/protocol/ivi-application-client-protocol.h"
#include "ozone/wayland/shell/shell.h"

namespace ozonewayland {

IVIShellSurface::IVIShellSurface()
    : WaylandShellSurface(), ivi_surface_(NULL) {}

IVIShellSurface::~IVIShellSurface() {
  ivi_surface_destroy(ivi_surface_);
}

void IVIShellSurface::InitializeShellSurface(WaylandWindow* window,
                                             WaylandWindow::ShellType type,
                                             int surface_id) {
  DCHECK(!ivi_surface_);
  WaylandDisplay* display = WaylandDisplay::GetInstance();
  DCHECK(display);
  WaylandShell* shell = WaylandDisplay::GetInstance()->GetShell();
  DCHECK(shell && shell->GetIVIShell());

  // The window_manager on AGL handles surface_id 0 as an invalid id.
  if (surface_id == 0)
    surface_id = static_cast<int>(getpid());

  ivi_surface_ = ivi_application_surface_create(shell->GetIVIShell(),
                                                surface_id, GetWLSurface());

  DCHECK(ivi_surface_);

  static const ivi_surface_listener ivi_surface_event_listener = {
    IVIShellSurface::HandleConfigure,
  };

  ivi_surface_add_listener(ivi_surface_,
                           &ivi_surface_event_listener, window);

}

void IVIShellSurface::UpdateShellSurface(WaylandWindow::ShellType type,
                                         WaylandShellSurface* shell_parent,
                                         int x,
                                         int y) {
  WaylandShellSurface::FlushDisplay();
}

void IVIShellSurface::SetWindowTitle(const base::string16& title) {
}

void IVIShellSurface::Maximize() {
}

void IVIShellSurface::Minimize() {
}

void IVIShellSurface::Unminimize() {
}

bool IVIShellSurface::IsMinimized() const {
  return false;
}

void IVIShellSurface::HandleConfigure(void* data,
                                     struct ivi_surface* surface,
                                     int32_t width,
                                     int32_t height) {
  WaylandShellSurface::WindowResized(data, width, height);
}

}  // namespace ozonewayland
