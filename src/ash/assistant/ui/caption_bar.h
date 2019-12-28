// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_ASSISTANT_UI_CAPTION_BAR_H_
#define ASH_ASSISTANT_UI_CAPTION_BAR_H_

#include <memory>

#include "base/component_export.h"
#include "base/macros.h"
#include "ui/events/event_observer.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view.h"

namespace views {
class EventMonitor;
}  // namespace views

namespace ash {

enum class AssistantButtonId;

// CaptionBarDelegate ----------------------------------------------------------

// TODO(wutao): Remove this class and call methods on AssistantViewDelegate
// derectly.
class COMPONENT_EXPORT(ASSISTANT_UI) CaptionBarDelegate {
 public:
  // Invoked when the caption button identified by |id| is pressed. Return
  // |true| to prevent default behavior from being performed, false otherwise.
  virtual bool OnCaptionButtonPressed(AssistantButtonId id) = 0;

 protected:
  virtual ~CaptionBarDelegate() = default;
};

// CaptionBar ------------------------------------------------------------------

class COMPONENT_EXPORT(ASSISTANT_UI) CaptionBar : public views::View,
                                                  public views::ButtonListener,
                                                  public ui::EventObserver {
 public:
  // This is necessary to inform clang that our overload of |OnEvent|,
  // overridden from |ui::EventObserver|, is intentional.
  using ui::EventHandler::OnEvent;

  CaptionBar();
  ~CaptionBar() override;

  // views::View:
  const char* GetClassName() const override;
  gfx::Size CalculatePreferredSize() const override;
  int GetHeightForWidth(int width) const override;
  void VisibilityChanged(views::View* starting_from, bool visible) override;

  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override;

  void set_delegate(CaptionBarDelegate* delegate) { delegate_ = delegate; }

  // Sets visibility for the caption button identified by |id|.
  void SetButtonVisible(AssistantButtonId id, bool visible);

 private:
  void InitLayout();
  void HandleButton(AssistantButtonId id);

  CaptionBarDelegate* delegate_ = nullptr;

  std::unique_ptr<views::EventMonitor> event_monitor_;

  DISALLOW_COPY_AND_ASSIGN(CaptionBar);
};

}  // namespace ash

#endif  // ASH_ASSISTANT_UI_CAPTION_BAR_H_
