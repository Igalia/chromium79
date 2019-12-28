// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_WM_SHADOW_CONTROLLER_DELEGATE_H_
#define ASH_WM_WM_SHADOW_CONTROLLER_DELEGATE_H_

#include "base/macros.h"
#include "ui/wm/core/shadow_controller_delegate.h"

namespace aura {
class Window;
}  // namespace aura

namespace ash {

// WmShadowControllerDelegate is a delegate for showing the shadow for window
// management purposes.
class WmShadowControllerDelegate : public ::wm::ShadowControllerDelegate {
 public:
  WmShadowControllerDelegate();
  ~WmShadowControllerDelegate() override;

  // ::wm::ShadowControllerDelegate:
  bool ShouldShowShadowForWindow(const aura::Window* window) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(WmShadowControllerDelegate);
};

}  // namespace ash

#endif  // ASH_WM_WM_SHADOW_CONTROLLER_DELEGATE_H_