// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/payments/verify_pending_dialog_view_impl.h"

#include "chrome/browser/ui/autofill/payments/verify_pending_dialog_controller.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "components/constrained_window/constrained_window_views.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/window/dialog_client_view.h"

namespace autofill {

VerifyPendingDialogViewImpl::VerifyPendingDialogViewImpl(
    VerifyPendingDialogController* controller)
    : controller_(controller) {
  // TODO(crbug.com/1014278): Should get correct width automatically when
  // snapping.
  const int width = ChromeLayoutProvider::Get()->GetDistanceMetric(
      DISTANCE_MODAL_DIALOG_PREFERRED_WIDTH);
  // TODO(crbug.com/1014334): Investigate why CalculatePreferredSize can not be
  // overridden when there is no layout manager.
  SetPreferredSize(gfx::Size(width, GetHeightForWidth(width)));
}

VerifyPendingDialogViewImpl::~VerifyPendingDialogViewImpl() {
  if (controller_) {
    controller_->OnDialogClosed();
    controller_ = nullptr;
  }
}

// static
VerifyPendingDialogView* VerifyPendingDialogView::CreateDialogAndShow(
    VerifyPendingDialogController* controller,
    content::WebContents* web_contents) {
  VerifyPendingDialogViewImpl* dialog =
      new VerifyPendingDialogViewImpl(controller);
  constrained_window::ShowWebModalDialogViews(dialog, web_contents);
  return dialog;
}

void VerifyPendingDialogViewImpl::Hide() {
  if (controller_) {
    controller_->OnDialogClosed();
    controller_ = nullptr;
  }
  GetWidget()->Close();
}

void VerifyPendingDialogViewImpl::AddedToWidget() {
  GetBubbleFrameView()->SetProgress(/*Infinite animation*/ -1);
}

bool VerifyPendingDialogViewImpl::Cancel() {
  if (controller_)
    controller_->OnCancel();

  return true;
}

int VerifyPendingDialogViewImpl::GetDialogButtons() const {
  return ui::DIALOG_BUTTON_CANCEL;
}

base::string16 VerifyPendingDialogViewImpl::GetDialogButtonLabel(
    ui::DialogButton button) const {
  DCHECK_EQ(button, ui::DIALOG_BUTTON_CANCEL);
  return controller_->GetCancelButtonLabel();
}

ui::ModalType VerifyPendingDialogViewImpl::GetModalType() const {
  return ui::MODAL_TYPE_CHILD;
}

base::string16 VerifyPendingDialogViewImpl::GetWindowTitle() const {
  return controller_->GetDialogTitle();
}

bool VerifyPendingDialogViewImpl::ShouldShowCloseButton() const {
  return false;
}

}  // namespace autofill
