// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_UI_LOGIN_SCREEN_EXTENSION_UI_LOGIN_SCREEN_EXTENSION_UI_WINDOW_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_UI_LOGIN_SCREEN_EXTENSION_UI_LOGIN_SCREEN_EXTENSION_UI_WINDOW_H_

#include <memory>

#include "base/macros.h"

namespace views {
class Widget;
}  // namespace views

namespace chromeos {

struct LoginScreenExtensionUiCreateOptions;
class LoginScreenExtensionUiDialogDelegate;
class LoginScreenExtensionUiWebDialogView;

// This class represents the window that can be created by the
// chrome.loginScreenUi API. It manages the window's widget, view and delegate,
// which are all automatically deleted when the widget closes.
// The window is not draggable, and has a close button which is not visible
// if |create_options.can_be_closed_by_user| is false.
class LoginScreenExtensionUiWindow {
 public:
  explicit LoginScreenExtensionUiWindow(
      LoginScreenExtensionUiCreateOptions* create_options);
  ~LoginScreenExtensionUiWindow();

  LoginScreenExtensionUiDialogDelegate* GetDialogDelegateForTesting();
  views::Widget* GetDialogWidgetForTesting();

 private:
  LoginScreenExtensionUiDialogDelegate* dialog_delegate_ = nullptr;
  LoginScreenExtensionUiWebDialogView* dialog_view_ = nullptr;
  views::Widget* dialog_widget_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(LoginScreenExtensionUiWindow);
};

class LoginScreenExtensionUiWindowFactory {
 public:
  LoginScreenExtensionUiWindowFactory();
  virtual ~LoginScreenExtensionUiWindowFactory();

  virtual std::unique_ptr<LoginScreenExtensionUiWindow> Create(
      LoginScreenExtensionUiCreateOptions* create_options);

  DISALLOW_COPY_AND_ASSIGN(LoginScreenExtensionUiWindowFactory);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_UI_LOGIN_SCREEN_EXTENSION_UI_LOGIN_SCREEN_EXTENSION_UI_WINDOW_H_
