// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/render_widget_host_view_aura.h"

#include <set>
#include <utility>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "cc/layers/layer.h"
#include "cc/trees/layer_tree_settings.h"
#include "components/viz/common/features.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/viz/common/surfaces/local_surface_id_allocation.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "content/browser/accessibility/browser_accessibility_state_impl.h"
#include "content/browser/bad_message.h"
#include "content/browser/compositor/surface_utils.h"
#include "content/browser/frame_host/frame_tree.h"
#include "content/browser/frame_host/frame_tree_node.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/gpu/compositor_util.h"
#include "content/browser/renderer_host/cursor_manager.h"
#include "content/browser/renderer_host/delegated_frame_host_client_aura.h"
#include "content/browser/renderer_host/dip_util.h"
#include "content/browser/renderer_host/input/synthetic_gesture_target_aura.h"
#include "content/browser/renderer_host/input/touch_selection_controller_client_aura.h"
#include "content/browser/renderer_host/overscroll_controller.h"
#include "content/browser/renderer_host/render_view_host_delegate.h"
#include "content/browser/renderer_host/render_view_host_delegate_view.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_input_event_router.h"
#include "content/browser/renderer_host/render_widget_host_view_event_handler.h"
#include "content/browser/renderer_host/ui_events_helper.h"
#include "content/common/input_messages.h"
#include "content/common/text_input_state.h"
#include "content/common/view_messages.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/common/content_features.h"
#include "content/public/common/use_zoom_for_dsf_policy.h"
#include "gpu/ipc/common/gpu_messages.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "third_party/blink/public/platform/web_input_event.h"
#include "third_party/blink/public/web/web_ime_text_span.h"
#include "ui/accessibility/accessibility_switches.h"
#include "ui/accessibility/platform/aura_window_properties.h"
#include "ui/accessibility/platform/ax_platform_node.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/client/cursor_client_observer.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/client/transient_window_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_event_dispatcher.h"
#include "ui/aura/window_observer.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/hit_test.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ui_base_switches.h"
#include "ui/base/ui_base_switches_util.h"
#include "ui/base/ui_base_types.h"
#include "ui/compositor/dip_util.h"
#include "ui/display/screen.h"
#include "ui/events/blink/blink_event_util.h"
#include "ui/events/blink/did_overscroll_params.h"
#include "ui/events/blink/web_input_event.h"
#include "ui/events/event.h"
#include "ui/events/event_observer.h"
#include "ui/events/event_utils.h"
#include "ui/events/gesture_detection/gesture_configuration.h"
#include "ui/events/gestures/gesture_recognizer.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/skia_util.h"
#include "ui/touch_selection/touch_selection_controller.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/wm/public/activation_client.h"
#include "ui/wm/public/scoped_tooltip_disabler.h"
#include "ui/wm/public/tooltip_client.h"

#if defined(OS_WIN)
#include "base/time/time.h"
#include "content/browser/accessibility/browser_accessibility_manager_win.h"
#include "content/browser/accessibility/browser_accessibility_win.h"
#include "content/browser/renderer_host/legacy_render_widget_host_win.h"
#include "ui/accessibility/platform/ax_fragment_root_win.h"
#include "ui/base/ime/input_method_keyboard_controller.h"
#include "ui/base/ime/input_method_keyboard_controller_observer.h"
#include "ui/base/win/hidden_window.h"
#include "ui/display/win/screen_win.h"
#include "ui/gfx/gdi_util.h"
#endif

#if defined(USE_X11)
#include "content/browser/accessibility/browser_accessibility_auralinux.h"
#endif

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
#include "ui/base/ime/linux/text_edit_command_auralinux.h"
#include "ui/base/ime/linux/text_edit_key_bindings_delegate_auralinux.h"
#endif

#if defined(OS_CHROMEOS)
#include "ui/wm/core/ime_util_chromeos.h"
#endif

#if defined(OS_FUCHSIA)
#include "ui/base/ime/input_method_keyboard_controller.h"
#endif

using gfx::RectToSkIRect;
using gfx::SkIRectToRect;

using blink::WebInputEvent;
using blink::WebGestureEvent;
using blink::WebTouchEvent;

namespace content {

namespace {

mojom::FrameInputHandler* GetFrameInputHandlerForFocusedFrame(
    RenderWidgetHostImpl* host) {
  if (!host || !host->delegate()) {
    return nullptr;
  }
  RenderFrameHostImpl* render_frame_host =
      host->delegate()->GetFocusedFrameFromFocusedDelegate();
  if (!render_frame_host)
    return nullptr;
  return render_frame_host->GetFrameInputHandler();
}

}  // namespace

#if defined(OS_WIN)

// This class implements the ui::InputMethodKeyboardControllerObserver interface
// which provides notifications about the on screen keyboard on Windows getting
// displayed or hidden in response to taps on editable fields.
// It provides functionality to request blink to scroll the input field if it
// is obscured by the on screen keyboard.
class WinScreenKeyboardObserver
    : public ui::InputMethodKeyboardControllerObserver {
 public:
  explicit WinScreenKeyboardObserver(RenderWidgetHostViewAura* host_view)
      : host_view_(host_view) {
    host_view_->SetInsets(gfx::Insets());
    if (auto* input_method = host_view_->GetInputMethod())
      input_method->GetInputMethodKeyboardController()->AddObserver(this);
  }

  ~WinScreenKeyboardObserver() override {
    if (auto* input_method = host_view_->GetInputMethod())
      input_method->GetInputMethodKeyboardController()->RemoveObserver(this);
  }

  // InputMethodKeyboardControllerObserver overrides.
  void OnKeyboardVisible(const gfx::Rect& keyboard_rect) override {
    host_view_->SetInsets(gfx::Insets(
        0, 0, keyboard_rect.IsEmpty() ? 0 : keyboard_rect.height(), 0));
  }

  void OnKeyboardHidden() override {
    // Restore the viewport.
    host_view_->SetInsets(gfx::Insets());
  }

 private:
  RenderWidgetHostViewAura* host_view_;

  DISALLOW_COPY_AND_ASSIGN(WinScreenKeyboardObserver);
};
#endif  // defined(OS_WIN)

// We need to watch for mouse events outside a Web Popup or its parent
// and dismiss the popup for certain events.
class RenderWidgetHostViewAura::EventObserverForPopupExit
    : public ui::EventObserver {
 public:
  explicit EventObserverForPopupExit(RenderWidgetHostViewAura* rwhva)
      : rwhva_(rwhva) {
    aura::Env* env = aura::Env::GetInstance();
    env->AddEventObserver(this, env,
                          {ui::ET_MOUSE_PRESSED, ui::ET_TOUCH_PRESSED});
  }

  ~EventObserverForPopupExit() override {
    aura::Env::GetInstance()->RemoveEventObserver(this);
  }

  // ui::EventObserver:
  void OnEvent(const ui::Event& event) override {
    rwhva_->ApplyEventObserverForPopupExit(*event.AsLocatedEvent());
  }

 private:
  RenderWidgetHostViewAura* rwhva_;

  DISALLOW_COPY_AND_ASSIGN(EventObserverForPopupExit);
};

void RenderWidgetHostViewAura::ApplyEventObserverForPopupExit(
    const ui::LocatedEvent& event) {
  DCHECK(event.type() == ui::ET_MOUSE_PRESSED ||
         event.type() == ui::ET_TOUCH_PRESSED);

  if (in_shutdown_ || is_fullscreen_)
    return;

  // |target| may be null.
  aura::Window* target = static_cast<aura::Window*>(event.target());
  if (target != window_ &&
      (!popup_parent_host_view_ ||
       target != popup_parent_host_view_->window_)) {
    // If we enter this code path it means that we did not receive any focus
    // lost notifications for the popup window. Ensure that blink is aware
    // of the fact that focus was lost for the host window by sending a Blur
    // notification. We also set a flag in the view indicating that we need
    // to force a Focus notification on the next mouse down.
    if (popup_parent_host_view_ && popup_parent_host_view_->host()) {
      popup_parent_host_view_->event_handler()
          ->set_focus_on_mouse_down_or_key_event(true);
      popup_parent_host_view_->host()->Blur();
    }
    // Note: popup_parent_host_view_ may be NULL when there are multiple
    // popup children per view. See: RenderWidgetHostViewAura::InitAsPopup().
    Shutdown();
  }
}

// We have to implement the WindowObserver interface on a separate object
// because clang doesn't like implementing multiple interfaces that have
// methods with the same name. This object is owned by the
// RenderWidgetHostViewAura.
class RenderWidgetHostViewAura::WindowObserver : public aura::WindowObserver {
 public:
  explicit WindowObserver(RenderWidgetHostViewAura* view)
      : view_(view) {
    view_->window_->AddObserver(this);
  }

  ~WindowObserver() override { view_->window_->RemoveObserver(this); }

  // Overridden from aura::WindowObserver:
  void OnWindowAddedToRootWindow(aura::Window* window) override {
    if (window == view_->window_)
      view_->AddedToRootWindow();
  }

  void OnWindowRemovingFromRootWindow(aura::Window* window,
                                      aura::Window* new_root) override {
    if (window == view_->window_)
      view_->RemovingFromRootWindow();
  }

  void OnWindowHierarchyChanged(const HierarchyChangeParams& params) override {
    view_->ParentHierarchyChanged();
  }

  void OnWindowTitleChanged(aura::Window* window) override {
    if (window == view_->window_)
      view_->WindowTitleChanged();
  }

 private:
  RenderWidgetHostViewAura* view_;

  DISALLOW_COPY_AND_ASSIGN(WindowObserver);
};

// This class provides functionality to observe the ancestors of the RWHVA for
// bounds changes. This is done to snap the RWHVA window to a pixel boundary,
// which could change when the bounds relative to the root changes.
// An example where this happens is below:-
// The fast resize code path for bookmarks where in the parent of RWHVA which
// is WCV has its bounds changed before the bookmark is hidden. This results in
// the traditional bounds change notification for the WCV reporting the old
// bounds as the bookmark is still around. Observing all the ancestors of the
// RWHVA window enables us to know when the bounds of the window relative to
// root changes and allows us to snap accordingly.
class RenderWidgetHostViewAura::WindowAncestorObserver
    : public aura::WindowObserver {
 public:
  explicit WindowAncestorObserver(RenderWidgetHostViewAura* view)
      : view_(view) {
    aura::Window* parent = view_->window_->parent();
    while (parent) {
      parent->AddObserver(this);
      ancestors_.insert(parent);
      parent = parent->parent();
    }
  }

  ~WindowAncestorObserver() override {
    RemoveAncestorObservers();
  }

  void OnWindowBoundsChanged(aura::Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds,
                             ui::PropertyChangeReason reason) override {
    DCHECK(ancestors_.find(window) != ancestors_.end());
    if (new_bounds.origin() != old_bounds.origin())
      view_->HandleParentBoundsChanged();
  }

 private:
  void RemoveAncestorObservers() {
    for (auto* ancestor : ancestors_)
      ancestor->RemoveObserver(this);
    ancestors_.clear();
  }

  RenderWidgetHostViewAura* view_;
  std::set<aura::Window*> ancestors_;

  DISALLOW_COPY_AND_ASSIGN(WindowAncestorObserver);
};


////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, public:

RenderWidgetHostViewAura::RenderWidgetHostViewAura(
    RenderWidgetHost* widget_host,
    bool is_guest_view_hack)
    : RenderWidgetHostViewBase(widget_host),
      window_(nullptr),
      in_shutdown_(false),
      in_bounds_changed_(false),
      popup_parent_host_view_(nullptr),
      popup_child_host_view_(nullptr),
      is_loading_(false),
      has_composition_text_(false),
      needs_begin_frames_(false),
      added_frame_observer_(false),
      cursor_visibility_state_in_renderer_(UNKNOWN),
#if defined(OS_WIN)
      legacy_render_widget_host_HWND_(nullptr),
      legacy_window_destroyed_(false),
      virtual_keyboard_requested_(false),
#endif
      is_guest_view_hack_(is_guest_view_hack),
      device_scale_factor_(0.0f),
      event_handler_(new RenderWidgetHostViewEventHandler(host(), this, this)),
      frame_sink_id_(is_guest_view_hack_ ? AllocateFrameSinkIdForGuestViewHack()
                                         : host()->GetFrameSinkId()) {
  CreateDelegatedFrameHostClient();

  if (!is_guest_view_hack_)
    host()->SetView(this);

  // We should start observing the TextInputManager for IME-related events as
  // well as monitoring its lifetime.
  if (GetTextInputManager())
    GetTextInputManager()->AddObserver(this);

  cursor_manager_.reset(new CursorManager(this));

  SetOverscrollControllerEnabled(
      base::FeatureList::IsEnabled(features::kOverscrollHistoryNavigation));

  selection_controller_client_.reset(
      new TouchSelectionControllerClientAura(this));
  CreateSelectionController();

  RenderWidgetHostOwnerDelegate* owner_delegate = host()->owner_delegate();
  if (owner_delegate) {
    // TODO(mostynb): actually use prefs.  Landing this as a separate CL
    // first to rebaseline some unreliable web tests.
    // NOTE: This will not be run for child frame widgets, which do not have
    // an owner delegate and won't get a RenderViewHost here.
    ignore_result(owner_delegate->GetWebkitPreferencesForWidget());
  }
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, RenderWidgetHostView implementation:

void RenderWidgetHostViewAura::InitAsChild(gfx::NativeView parent_view) {
  DCHECK_EQ(widget_type_, WidgetType::kFrame);
  CreateAuraWindow(aura::client::WINDOW_TYPE_CONTROL);

  if (parent_view)
    parent_view->AddChild(GetNativeView());

  device_scale_factor_ = GetDeviceScaleFactor();
}

void RenderWidgetHostViewAura::InitAsPopup(
    RenderWidgetHostView* parent_host_view,
    const gfx::Rect& bounds_in_screen) {
  DCHECK_EQ(widget_type_, WidgetType::kPopup);
  DCHECK(!static_cast<RenderWidgetHostViewBase*>(parent_host_view)
              ->IsRenderWidgetHostViewChildFrame());

  popup_parent_host_view_ =
      static_cast<RenderWidgetHostViewAura*>(parent_host_view);

  // TransientWindowClient may be NULL during tests.
  aura::client::TransientWindowClient* transient_window_client =
      aura::client::GetTransientWindowClient();
  RenderWidgetHostViewAura* old_child =
      popup_parent_host_view_->popup_child_host_view_;
  if (old_child) {
    // TODO(jhorwich): Allow multiple popup_child_host_view_ per view, or
    // similar mechanism to ensure a second popup doesn't cause the first one
    // to never get a chance to filter events. See crbug.com/160589.
    DCHECK(old_child->popup_parent_host_view_ == popup_parent_host_view_);
    if (transient_window_client) {
      transient_window_client->RemoveTransientChild(
        popup_parent_host_view_->window_, old_child->window_);
    }
    old_child->popup_parent_host_view_ = nullptr;
  }
  popup_parent_host_view_->SetPopupChild(this);
  CreateAuraWindow(aura::client::WINDOW_TYPE_MENU);

  // Setting the transient child allows for the popup to get mouse events when
  // in a system modal dialog. Do this before calling ParentWindowWithContext
  // below so that the transient parent is visible to WindowTreeClient.
  // This fixes crbug.com/328593.
  if (transient_window_client) {
    transient_window_client->AddTransientChild(
        popup_parent_host_view_->window_, window_);
  }

  aura::Window* root = popup_parent_host_view_->window_->GetRootWindow();
  aura::client::ParentWindowWithContext(window_, root, bounds_in_screen);

  SetBounds(bounds_in_screen);
  Show();
  if (NeedsMouseCapture())
    window_->SetCapture();

  event_observer_for_popup_exit_ =
      std::make_unique<EventObserverForPopupExit>(this);

  device_scale_factor_ = GetDeviceScaleFactor();
}

void RenderWidgetHostViewAura::InitAsFullscreen(
    RenderWidgetHostView* reference_host_view) {
  DCHECK_EQ(widget_type_, WidgetType::kFrame);
  is_fullscreen_ = true;
  CreateAuraWindow(aura::client::WINDOW_TYPE_NORMAL);
  window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_FULLSCREEN);

  aura::Window* parent = nullptr;
  gfx::Rect bounds;
  if (reference_host_view) {
    aura::Window* reference_window =
        static_cast<RenderWidgetHostViewAura*>(reference_host_view)->window_;
    event_handler_->TrackHost(reference_window);
    display::Display display =
        display::Screen::GetScreen()->GetDisplayNearestWindow(reference_window);
    parent = reference_window->GetRootWindow();
    bounds = display.bounds();
  }
  aura::client::ParentWindowWithContext(window_, parent, bounds);
  Show();
  Focus();

  device_scale_factor_ = GetDeviceScaleFactor();
}

void RenderWidgetHostViewAura::Show() {
  // If the viz::LocalSurfaceIdAllocation is invalid, we may have been evicted,
  // and no other visual properties have since been changed. Allocate a new id
  // and start synchronizing.
  if (!window_->GetLocalSurfaceIdAllocation().IsValid()) {
    window_->AllocateLocalSurfaceId();
    SynchronizeVisualProperties(cc::DeadlinePolicy::UseDefaultDeadline(),
                                window_->GetLocalSurfaceIdAllocation());
  }

  window_->Show();
  WasUnOccluded();
}

void RenderWidgetHostViewAura::Hide() {
  window_->Hide();
  WasOccluded();
}

void RenderWidgetHostViewAura::SetSize(const gfx::Size& size) {
  // For a SetSize operation, we don't care what coordinate system the origin
  // of the window is in, it's only important to make sure that the origin
  // remains constant after the operation.
  InternalSetBounds(gfx::Rect(window_->bounds().origin(), size));
}

void RenderWidgetHostViewAura::SetBounds(const gfx::Rect& rect) {
  gfx::Point relative_origin(rect.origin());

  // RenderWidgetHostViewAura::SetBounds() takes screen coordinates, but
  // Window::SetBounds() takes parent coordinates, so do the conversion here.
  aura::Window* root = window_->GetRootWindow();
  if (root) {
    aura::client::ScreenPositionClient* screen_position_client =
        aura::client::GetScreenPositionClient(root);
    if (screen_position_client) {
      screen_position_client->ConvertPointFromScreen(window_->parent(),
                                                     &relative_origin);
    }
  }

  InternalSetBounds(gfx::Rect(relative_origin, rect.size()));
}

gfx::NativeView RenderWidgetHostViewAura::GetNativeView() {
  return window_;
}

#if defined(OS_WIN)
HWND RenderWidgetHostViewAura::GetHostWindowHWND() const {
  aura::WindowTreeHost* host = window_->GetHost();
  return host ? host->GetAcceleratedWidget() : nullptr;
}
#endif

gfx::NativeViewAccessible RenderWidgetHostViewAura::GetNativeViewAccessible() {
#if defined(OS_WIN)
  aura::WindowTreeHost* window_host = window_->GetHost();
  if (!window_host)
    return static_cast<gfx::NativeViewAccessible>(NULL);

  if (legacy_render_widget_host_HWND_)
    return legacy_render_widget_host_HWND_->GetOrCreateWindowRootAccessible();

  BrowserAccessibilityManager* manager =
      host()->GetOrCreateRootBrowserAccessibilityManager();
  if (manager)
    return ToBrowserAccessibilityWin(manager->GetRoot())->GetCOM();

#elif defined(USE_X11)
  BrowserAccessibilityManager* manager =
      host()->GetOrCreateRootBrowserAccessibilityManager();
  if (manager && manager->GetRoot())
    return manager->GetRoot()->GetNativeViewAccessible();
#endif

  NOTIMPLEMENTED_LOG_ONCE();
  return static_cast<gfx::NativeViewAccessible>(nullptr);
}

ui::TextInputClient* RenderWidgetHostViewAura::GetTextInputClient() {
  return this;
}

void RenderWidgetHostViewAura::SetNeedsBeginFrames(bool needs_begin_frames) {
  needs_begin_frames_ = needs_begin_frames;
  UpdateNeedsBeginFramesInternal();
}

void RenderWidgetHostViewAura::SetWantsAnimateOnlyBeginFrames() {
  if (delegated_frame_host_)
    delegated_frame_host_->SetWantsAnimateOnlyBeginFrames();
}

void RenderWidgetHostViewAura::OnBeginFrame(base::TimeTicks frame_time) {
  host()->ProgressFlingIfNeeded(frame_time);
  UpdateNeedsBeginFramesInternal();
}

RenderFrameHostImpl* RenderWidgetHostViewAura::GetFocusedFrame() const {
  RenderWidgetHostOwnerDelegate* owner_delegate = host()->owner_delegate();
  // TODO(crbug.com/689777): Child local roots do not work here?
  if (!owner_delegate)
    return nullptr;
  FrameTreeNode* focused_frame = owner_delegate->GetFocusedFrame();
  if (!focused_frame)
    return nullptr;
  return focused_frame->current_frame_host();
}

void RenderWidgetHostViewAura::HandleParentBoundsChanged() {
#if defined(OS_WIN)
  if (legacy_render_widget_host_HWND_) {
    legacy_render_widget_host_HWND_->SetBounds(
        window_->GetBoundsInRootWindow());
  }
#endif
  if (!in_shutdown_) {
    // Send screen rects through the delegate if there is one. Not every
    // RenderWidgetHost has a delegate (for example, drop-down widgets).
    if (host_->delegate())
      host_->delegate()->SendScreenRects();
    else
      host_->SendScreenRects();
  }
}

void RenderWidgetHostViewAura::ParentHierarchyChanged() {
  ancestor_window_observer_.reset(new WindowAncestorObserver(this));
  // Snap when we receive a hierarchy changed. http://crbug.com/388908.
  HandleParentBoundsChanged();
}

void RenderWidgetHostViewAura::Focus() {
  // Make sure we have a FocusClient before attempting to Focus(). In some
  // situations we may not yet be in a valid Window hierarchy (such as reloading
  // after out of memory discarded the tab).
  aura::client::FocusClient* client = aura::client::GetFocusClient(window_);
  if (client)
    window_->Focus();
}

bool RenderWidgetHostViewAura::HasFocus() {
  return window_->HasFocus();
}

bool RenderWidgetHostViewAura::IsSurfaceAvailableForCopy() {
  if (!delegated_frame_host_)
    return false;
  return delegated_frame_host_->CanCopyFromCompositingSurface();
}

void RenderWidgetHostViewAura::EnsureSurfaceSynchronizedForWebTest() {
  ++latest_capture_sequence_number_;
  SynchronizeVisualProperties(cc::DeadlinePolicy::UseInfiniteDeadline(),
                              base::nullopt);
}

bool RenderWidgetHostViewAura::IsShowing() {
  return window_->IsVisible();
}

void RenderWidgetHostViewAura::WasUnOccluded() {
  if (!host_->is_hidden())
    return;

  auto tab_switch_start_state = TakeRecordTabSwitchTimeRequest();
  bool has_saved_frame =
      delegated_frame_host_ ? delegated_frame_host_->HasSavedFrame() : false;

  host()->WasShown(has_saved_frame ? base::nullopt : tab_switch_start_state);

  aura::Window* root = window_->GetRootWindow();
  if (root) {
    aura::client::CursorClient* cursor_client =
        aura::client::GetCursorClient(root);
    if (cursor_client)
      NotifyRendererOfCursorVisibilityState(cursor_client->IsCursorVisible());
  }

  if (delegated_frame_host_) {
    // If the frame for the renderer is already available, then the
    // tab-switching time is the presentation time for the browser-compositor.
    delegated_frame_host_->WasShown(
        GetLocalSurfaceIdAllocation().local_surface_id(),
        window_->bounds().size(),
        has_saved_frame ? tab_switch_start_state : base::nullopt);
  }

#if defined(OS_WIN)
  UpdateLegacyWin();
#endif
}

void RenderWidgetHostViewAura::WasOccluded() {
  if (!host()->is_hidden()) {
    host()->WasHidden();
    aura::WindowTreeHost* host = window_->GetHost();
    if (delegated_frame_host_) {
      aura::Window* parent = window_->parent();
      aura::Window::OcclusionState parent_occl_state =
          parent ? parent->occlusion_state()
                 : aura::Window::OcclusionState::UNKNOWN;
      aura::Window::OcclusionState native_win_occlusion_state =
          host ? host->GetNativeWindowOcclusionState()
               : aura::Window::OcclusionState::UNKNOWN;
      DelegatedFrameHost::HiddenCause cause;
      if (parent_occl_state == aura::Window::OcclusionState::OCCLUDED &&
          native_win_occlusion_state ==
              aura::Window::OcclusionState::OCCLUDED) {
        cause = DelegatedFrameHost::HiddenCause::kOccluded;
      } else {
        cause = DelegatedFrameHost::HiddenCause::kOther;
      }
      delegated_frame_host_->WasHidden(cause);
    }
#if defined(OS_WIN)
    if (host) {
      // We reparent the legacy Chrome_RenderWidgetHostHWND window to the global
      // hidden window on the same lines as Windowed plugin windows.
      if (legacy_render_widget_host_HWND_)
        legacy_render_widget_host_HWND_->UpdateParent(ui::GetHiddenWindow());
    }
#endif
  }

#if defined(OS_WIN)
  if (legacy_render_widget_host_HWND_)
    legacy_render_widget_host_HWND_->Hide();
#endif
}

bool RenderWidgetHostViewAura::ShouldShowStaleContentOnEviction() {
  return host() && host()->ShouldShowStaleContentOnEviction();
}

gfx::Rect RenderWidgetHostViewAura::GetViewBounds() {
  return window_->GetBoundsInScreen();
}

void RenderWidgetHostViewAura::UpdateBackgroundColor() {
  DCHECK(GetBackgroundColor());

  SkColor color = *GetBackgroundColor();
  bool opaque = SkColorGetA(color) == SK_AlphaOPAQUE;
  window_->layer()->SetFillsBoundsOpaquely(opaque);
  window_->layer()->SetColor(color);
}

void RenderWidgetHostViewAura::WindowTitleChanged() {
  if (delegated_frame_host_) {
    delegated_frame_host_->WindowTitleChanged(
        base::UTF16ToUTF8(window_->GetTitle()));
  }
}

bool RenderWidgetHostViewAura::IsMouseLocked() {
  return event_handler_->mouse_locked();
}

gfx::Size RenderWidgetHostViewAura::GetVisibleViewportSize() {
  gfx::Rect requested_rect(GetRequestedRendererSize());
  requested_rect.Inset(insets_);
  return requested_rect.size();
}

void RenderWidgetHostViewAura::SetInsets(const gfx::Insets& insets) {
  if (insets != insets_) {
    insets_ = insets;
    window_->AllocateLocalSurfaceId();
    if (!insets.IsEmpty()) {
      inset_surface_id_allocation_ = window_->GetLocalSurfaceIdAllocation();
    } else {
      inset_surface_id_allocation_ = viz::LocalSurfaceIdAllocation();
    }
    SynchronizeVisualProperties(cc::DeadlinePolicy::UseDefaultDeadline(),
                                window_->GetLocalSurfaceIdAllocation());
  }
}

void RenderWidgetHostViewAura::FocusedNodeTouched(bool editable) {
#if defined(OS_WIN)
  auto* input_method = GetInputMethod();
  if (!input_method || !input_method->GetInputMethodKeyboardController())
    return;
  auto* controller = input_method->GetInputMethodKeyboardController();
  if (editable && host()->GetView() && host()->delegate()) {
    if (last_pointer_type_ == ui::EventPointerType::POINTER_TYPE_TOUCH) {
      keyboard_observer_.reset(new WinScreenKeyboardObserver(this));
      if (!controller->DisplayVirtualKeyboard())
        keyboard_observer_.reset(nullptr);
    } else {
      keyboard_observer_.reset(nullptr);
    }
    virtual_keyboard_requested_ = keyboard_observer_.get();
  } else {
    virtual_keyboard_requested_ = false;
    controller->DismissVirtualKeyboard();
  }
#endif
}

void RenderWidgetHostViewAura::UpdateCursor(const WebCursor& cursor) {
  GetCursorManager()->UpdateCursor(this, cursor);
}

void RenderWidgetHostViewAura::DisplayCursor(const WebCursor& cursor) {
  current_cursor_ = cursor;
  const display::Display display =
      display::Screen::GetScreen()->GetDisplayNearestWindow(window_);
  current_cursor_.SetDisplayInfo(display);
  UpdateCursorIfOverSelf();
}

CursorManager* RenderWidgetHostViewAura::GetCursorManager() {
  return cursor_manager_.get();
}

void RenderWidgetHostViewAura::SetIsLoading(bool is_loading) {
  is_loading_ = is_loading;
  UpdateCursorIfOverSelf();
}

void RenderWidgetHostViewAura::RenderProcessGone() {
  UpdateCursorIfOverSelf();
  Destroy();
}

void RenderWidgetHostViewAura::Destroy() {
  // Beware, this function is not called on all destruction paths. If |window_|
  // has been created, then it will implicitly end up calling
  // ~RenderWidgetHostViewAura when |window_| is destroyed. Otherwise, The
  // destructor is invoked directly from here. So all destruction/cleanup code
  // should happen there, not here.
  in_shutdown_ = true;
  if (window_)
    delete window_;
  else
    delete this;
}

void RenderWidgetHostViewAura::SetTooltipText(
    const base::string16& tooltip_text) {
  GetCursorManager()->SetTooltipTextForView(this, tooltip_text);
}

void RenderWidgetHostViewAura::DisplayTooltipText(
    const base::string16& tooltip_text) {
  tooltip_ = tooltip_text;
  aura::Window* root_window = window_->GetRootWindow();
  wm::TooltipClient* tooltip_client = wm::GetTooltipClient(root_window);
  if (tooltip_client) {
    tooltip_client->UpdateTooltip(window_);
    // Content tooltips should be visible indefinitely.
    tooltip_client->SetTooltipShownTimeout(window_, 0);
  }
}

uint32_t RenderWidgetHostViewAura::GetCaptureSequenceNumber() const {
  return latest_capture_sequence_number_;
}

void RenderWidgetHostViewAura::CopyFromSurface(
    const gfx::Rect& src_subrect,
    const gfx::Size& dst_size,
    base::OnceCallback<void(const SkBitmap&)> callback) {
  base::WeakPtr<RenderWidgetHostImpl> popup_host;
  base::WeakPtr<DelegatedFrameHost> popup_frame_host;
  if (popup_child_host_view_) {
    popup_host = popup_child_host_view_->host()->GetWeakPtr();
    popup_frame_host =
        popup_child_host_view_->GetDelegatedFrameHost()->GetWeakPtr();
  }
  RenderWidgetHostViewBase::CopyMainAndPopupFromSurface(
      host()->GetWeakPtr(), delegated_frame_host_->GetWeakPtr(), popup_host,
      popup_frame_host, src_subrect, dst_size, device_scale_factor_,
      std::move(callback));
}

#if defined(OS_WIN)
bool RenderWidgetHostViewAura::UsesNativeWindowFrame() const {
  return (legacy_render_widget_host_HWND_ != NULL);
}

void RenderWidgetHostViewAura::UpdateMouseLockRegion() {
  RECT window_rect =
      display::Screen::GetScreen()
          ->DIPToScreenRectInWindow(window_, window_->GetBoundsInScreen())
          .ToRECT();
  ::ClipCursor(&window_rect);
}

void RenderWidgetHostViewAura::OnLegacyWindowDestroyed() {
  legacy_render_widget_host_HWND_ = nullptr;
  legacy_window_destroyed_ = true;
}

gfx::NativeViewAccessible
RenderWidgetHostViewAura::GetParentNativeViewAccessible() {
  if (window_->parent()) {
    return window_->parent()->GetProperty(
        aura::client::kParentNativeViewAccessibleKey);
  }

  return nullptr;
}
#endif

void RenderWidgetHostViewAura::DidCreateNewRendererCompositorFrameSink(
    viz::mojom::CompositorFrameSinkClient* renderer_compositor_frame_sink) {
  renderer_compositor_frame_sink_ = renderer_compositor_frame_sink;
  if (delegated_frame_host_) {
    delegated_frame_host_->DidCreateNewRendererCompositorFrameSink(
        renderer_compositor_frame_sink_);
  }
}

void RenderWidgetHostViewAura::SubmitCompositorFrame(
    const viz::LocalSurfaceId& local_surface_id,
    viz::CompositorFrame frame,
    base::Optional<viz::HitTestRegionList> hit_test_region_list) {
  DCHECK(delegated_frame_host_);
  TRACE_EVENT0("content", "RenderWidgetHostViewAura::OnSwapCompositorFrame");

  delegated_frame_host_->SubmitCompositorFrame(
      local_surface_id, std::move(frame), std::move(hit_test_region_list));
}

void RenderWidgetHostViewAura::OnDidNotProduceFrame(
    const viz::BeginFrameAck& ack) {
  if (delegated_frame_host_)
    delegated_frame_host_->DidNotProduceFrame(ack);
}

void RenderWidgetHostViewAura::ResetFallbackToFirstNavigationSurface() {
  if (delegated_frame_host_)
    delegated_frame_host_->ResetFallbackToFirstNavigationSurface();
}

bool RenderWidgetHostViewAura::RequestRepaintForTesting() {
  return SynchronizeVisualProperties(cc::DeadlinePolicy::UseDefaultDeadline(),
                                     base::nullopt);
}

void RenderWidgetHostViewAura::DidStopFlinging() {
  selection_controller_client_->OnScrollCompleted();
}

void RenderWidgetHostViewAura::TransformPointToRootSurface(gfx::PointF* point) {
  aura::Window* root = window_->GetRootWindow();
  aura::Window::ConvertPointToTarget(window_, root, point);
  root->GetRootWindow()->transform().TransformPoint(point);
}

gfx::Rect RenderWidgetHostViewAura::GetBoundsInRootWindow() {
  aura::Window* top_level = window_->GetToplevelWindow();
  gfx::Rect bounds(top_level->GetBoundsInScreen());

#if defined(OS_WIN)
  // TODO(zturner,iyengar): This will break when we remove support for NPAPI and
  // remove the legacy hwnd, so a better fix will need to be decided when that
  // happens.
  if (UsesNativeWindowFrame()) {
    // aura::Window doesn't take into account non-client area of native windows
    // (e.g. HWNDs), so for that case ask Windows directly what the bounds are.
    aura::WindowTreeHost* host = top_level->GetHost();
    if (!host)
      return top_level->GetBoundsInScreen();
    RECT window_rect = {0};
    HWND hwnd = host->GetAcceleratedWidget();
    ::GetWindowRect(hwnd, &window_rect);
    bounds = gfx::Rect(window_rect);

    // Maximized windows are outdented from the work area by the frame thickness
    // even though this "frame" is not painted.  This confuses code (and people)
    // that think of a maximized window as corresponding exactly to the work
    // area.  Correct for this by subtracting the frame thickness back off.
    if (::IsZoomed(hwnd)) {
      bounds.Inset(GetSystemMetrics(SM_CXSIZEFRAME),
                   GetSystemMetrics(SM_CYSIZEFRAME));

      bounds.Inset(GetSystemMetrics(SM_CXPADDEDBORDER),
                   GetSystemMetrics(SM_CXPADDEDBORDER));
    }

    // Pixels come back from GetWindowHost, so we need to convert those back to
    // DIPs here.
    bounds = display::Screen::GetScreen()->ScreenToDIPRectInWindow(top_level,
                                                                   bounds);
  }

#endif

  return bounds;
}

void RenderWidgetHostViewAura::WheelEventAck(
    const blink::WebMouseWheelEvent& event,
    InputEventAckState ack_result) {
  if (overscroll_controller_) {
    overscroll_controller_->ReceivedEventACK(
        event, (INPUT_EVENT_ACK_STATE_CONSUMED == ack_result));
  }
}

void RenderWidgetHostViewAura::DidOverscroll(
    const ui::DidOverscrollParams& params) {
  if (overscroll_controller_)
    overscroll_controller_->OnDidOverscroll(params);
}

void RenderWidgetHostViewAura::GestureEventAck(
    const blink::WebGestureEvent& event,
    InputEventAckState ack_result) {
  const blink::WebInputEvent::Type event_type = event.GetType();
  if (event_type == blink::WebGestureEvent::kGestureScrollBegin ||
      event_type == blink::WebGestureEvent::kGestureScrollEnd) {
    if (host()->delegate()) {
      host()->delegate()->SetTopControlsGestureScrollInProgress(
          event_type == blink::WebGestureEvent::kGestureScrollBegin);
    }
  }

  if (overscroll_controller_) {
    overscroll_controller_->ReceivedEventACK(
        event, (INPUT_EVENT_ACK_STATE_CONSUMED == ack_result));
    // Terminate an active fling when the ACK for a GSU generated from the fling
    // progress (GSU with inertial state) is consumed and the overscrolling mode
    // is not |OVERSCROLL_NONE|. The early fling termination generates a GSE
    // which completes the overscroll action. Without this change the overscroll
    // action would complete at the end of the active fling progress which
    // causes noticeable delay in cases that the fling velocity is large.
    // https://crbug.com/797855
    if (event_type == blink::WebInputEvent::kGestureScrollUpdate &&
        event.data.scroll_update.inertial_phase ==
            blink::WebGestureEvent::InertialPhaseState::kMomentum &&
        overscroll_controller_->overscroll_mode() != OVERSCROLL_NONE) {
      StopFling();
    }
  }

  // Stop flinging if a GSU event with momentum phase is sent to the renderer
  // but not consumed.
  StopFlingingIfNecessary(event, ack_result);

  event_handler_->GestureEventAck(event, ack_result);

  ForwardTouchpadZoomEventIfNecessary(event, ack_result);
}

void RenderWidgetHostViewAura::ProcessAckedTouchEvent(
    const TouchEventWithLatencyInfo& touch,
    InputEventAckState ack_result) {
  aura::WindowTreeHost* window_host = window_->GetHost();
  // |host| is NULL during tests.
  if (!window_host)
    return;

  // The TouchScrollStarted event is generated & consumed downstream from the
  // TouchEventQueue. So we don't expect an ACK up here.
  DCHECK(touch.event.GetType() != blink::WebInputEvent::kTouchScrollStarted);

  ui::EventResult result = (ack_result == INPUT_EVENT_ACK_STATE_CONSUMED)
                               ? ui::ER_HANDLED
                               : ui::ER_UNHANDLED;

  blink::WebTouchPoint::State required_state;
  switch (touch.event.GetType()) {
    case blink::WebInputEvent::kTouchStart:
      required_state = blink::WebTouchPoint::kStatePressed;
      break;
    case blink::WebInputEvent::kTouchEnd:
      required_state = blink::WebTouchPoint::kStateReleased;
      break;
    case blink::WebInputEvent::kTouchMove:
      required_state = blink::WebTouchPoint::kStateMoved;
      break;
    case blink::WebInputEvent::kTouchCancel:
      required_state = blink::WebTouchPoint::kStateCancelled;
      break;
    default:
      required_state = blink::WebTouchPoint::kStateUndefined;
      NOTREACHED();
      break;
  }

  // Only send acks for one changed touch point.
  bool sent_ack = false;
  for (size_t i = 0; i < touch.event.touches_length; ++i) {
    if (touch.event.touches[i].state == required_state) {
      DCHECK(!sent_ack);
      window_host->dispatcher()->ProcessedTouchEvent(
          touch.event.unique_touch_event_id, window_, result,
          InputEventAckStateIsSetNonBlocking(ack_result));
      if (touch.event.touch_start_or_first_touch_move &&
          result == ui::ER_HANDLED && host()->delegate() &&
          host()->delegate()->GetInputEventRouter()) {
        host()
            ->delegate()
            ->GetInputEventRouter()
            ->OnHandledTouchStartOrFirstTouchMove(
                touch.event.unique_touch_event_id);
      }
      sent_ack = true;
    }
  }
}

std::unique_ptr<SyntheticGestureTarget>
RenderWidgetHostViewAura::CreateSyntheticGestureTarget() {
  return std::unique_ptr<SyntheticGestureTarget>(
      new SyntheticGestureTargetAura(host()));
}

InputEventAckState RenderWidgetHostViewAura::FilterInputEvent(
    const blink::WebInputEvent& input_event) {
  bool consumed = false;
  if (input_event.GetType() == WebInputEvent::kGestureFlingStart) {
    const WebGestureEvent& gesture_event =
        static_cast<const WebGestureEvent&>(input_event);
    // Zero-velocity touchpad flings are an Aura-specific signal that the
    // touchpad scroll has ended, and should not be forwarded to the renderer.
    if (gesture_event.SourceDevice() == blink::WebGestureDevice::kTouchpad &&
        !gesture_event.data.fling_start.velocity_x &&
        !gesture_event.data.fling_start.velocity_y) {
      consumed = true;
    }
  }

  if (overscroll_controller_)
    consumed |= overscroll_controller_->WillHandleEvent(input_event);

  // Touch events should always propagate to the renderer.
  if (WebTouchEvent::IsTouchEventType(input_event.GetType()))
    return INPUT_EVENT_ACK_STATE_NOT_CONSUMED;

  if (consumed &&
      input_event.GetType() == blink::WebInputEvent::kGestureFlingStart) {
    // Here we indicate that there was no consumer for this event, as
    // otherwise the fling animation system will try to run an animation
    // and will also expect a notification when the fling ends. Since
    // CrOS just uses the GestureFlingStart with zero-velocity as a means
    // of indicating that touchpad scroll has ended, we don't actually want
    // a fling animation. Note: Similar code exists in
    // RenderWidgetHostViewChildFrame::FilterInputEvent()
    return INPUT_EVENT_ACK_STATE_NO_CONSUMER_EXISTS;
  }

  return consumed ? INPUT_EVENT_ACK_STATE_CONSUMED
                  : INPUT_EVENT_ACK_STATE_NOT_CONSUMED;
}

InputEventAckState RenderWidgetHostViewAura::FilterChildGestureEvent(
    const blink::WebGestureEvent& gesture_event) {
  if (overscroll_controller_ &&
      overscroll_controller_->WillHandleEvent(gesture_event))
    return INPUT_EVENT_ACK_STATE_CONSUMED;
  return INPUT_EVENT_ACK_STATE_NOT_CONSUMED;
}

BrowserAccessibilityManager*
RenderWidgetHostViewAura::CreateBrowserAccessibilityManager(
    BrowserAccessibilityDelegate* delegate,
    bool for_root_frame) {
  BrowserAccessibilityManager* manager = nullptr;
#if defined(OS_WIN)
  manager = new BrowserAccessibilityManagerWin(
      BrowserAccessibilityManagerWin::GetEmptyDocument(), delegate);
#else
  manager = BrowserAccessibilityManager::Create(
      BrowserAccessibilityManager::GetEmptyDocument(), delegate);
#endif
  return manager;
}

gfx::AcceleratedWidget
RenderWidgetHostViewAura::AccessibilityGetAcceleratedWidget() {
#if defined(OS_WIN)
  if (legacy_render_widget_host_HWND_)
    return legacy_render_widget_host_HWND_->hwnd();
#endif
  return gfx::kNullAcceleratedWidget;
}

gfx::NativeViewAccessible
RenderWidgetHostViewAura::AccessibilityGetNativeViewAccessible() {
#if defined(OS_WIN)
  if (legacy_render_widget_host_HWND_) {
    if (switches::IsExperimentalAccessibilityPlatformUIAEnabled()) {
      ui::AXFragmentRootWin* fragment_root =
          ui::AXFragmentRootWin::GetForAcceleratedWidget(
              legacy_render_widget_host_HWND_->hwnd());
      if (fragment_root)
        return fragment_root->GetNativeViewAccessible();
    } else {
      return legacy_render_widget_host_HWND_->window_accessible();
    }
  }
#endif

  if (window_->parent()) {
    return window_->parent()->GetProperty(
        aura::client::kParentNativeViewAccessibleKey);
  }

  return nullptr;
}

void RenderWidgetHostViewAura::SetMainFrameAXTreeID(ui::AXTreeID id) {
  window_->SetProperty(ui::kChildAXTreeID, id.ToString());
}

bool RenderWidgetHostViewAura::LockMouse(bool request_unadjusted_movement) {
  return event_handler_->LockMouse(request_unadjusted_movement);
}

void RenderWidgetHostViewAura::UnlockMouse() {
  event_handler_->UnlockMouse();
}

bool RenderWidgetHostViewAura::GetIsMouseLockedUnadjustedMovementForTesting() {
  return event_handler_->mouse_locked_unadjusted_movement();
}

bool RenderWidgetHostViewAura::LockKeyboard(
    base::Optional<base::flat_set<ui::DomCode>> codes) {
  return event_handler_->LockKeyboard(std::move(codes));
}

void RenderWidgetHostViewAura::UnlockKeyboard() {
  event_handler_->UnlockKeyboard();
}

bool RenderWidgetHostViewAura::IsKeyboardLocked() {
  return event_handler_->IsKeyboardLocked();
}

base::flat_map<std::string, std::string>
RenderWidgetHostViewAura::GetKeyboardLayoutMap() {
  aura::WindowTreeHost* host = window_->GetHost();
  if (host)
    return host->GetKeyboardLayoutMap();
  return {};
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, ui::TextInputClient implementation:
void RenderWidgetHostViewAura::SetCompositionText(
    const ui::CompositionText& composition) {
  if (!text_input_manager_ || !text_input_manager_->GetActiveWidget())
    return;

  // TODO(suzhe): due to a bug of webkit, we can't use selection range with
  // composition string. See: https://bugs.webkit.org/show_bug.cgi?id=37788
  text_input_manager_->GetActiveWidget()->ImeSetComposition(
      composition.text, composition.ime_text_spans, gfx::Range::InvalidRange(),
      composition.selection.end(), composition.selection.end());

  has_composition_text_ = !composition.text.empty();
}

void RenderWidgetHostViewAura::ConfirmCompositionText() {
  if (text_input_manager_ && text_input_manager_->GetActiveWidget() &&
      has_composition_text_) {
    text_input_manager_->GetActiveWidget()->ImeFinishComposingText(false);
  }
  has_composition_text_ = false;
}

void RenderWidgetHostViewAura::ClearCompositionText() {
  if (text_input_manager_ && text_input_manager_->GetActiveWidget() &&
      has_composition_text_)
    text_input_manager_->GetActiveWidget()->ImeCancelComposition();
  has_composition_text_ = false;
}

void RenderWidgetHostViewAura::InsertText(const base::string16& text) {
  DCHECK_NE(GetTextInputType(), ui::TEXT_INPUT_TYPE_NONE);

  if (text_input_manager_ && text_input_manager_->GetActiveWidget()) {
    if (text.length())
      text_input_manager_->GetActiveWidget()->ImeCommitText(
          text, std::vector<ui::ImeTextSpan>(), gfx::Range::InvalidRange(), 0);
    else if (has_composition_text_)
      text_input_manager_->GetActiveWidget()->ImeFinishComposingText(false);
  }
  has_composition_text_ = false;
}

void RenderWidgetHostViewAura::InsertChar(const ui::KeyEvent& event) {
  if (popup_child_host_view_ && popup_child_host_view_->NeedsInputGrab()) {
    popup_child_host_view_->InsertChar(event);
    return;
  }

  // Ignore character messages for VKEY_RETURN sent on CTRL+M. crbug.com/315547
  if (event_handler_->accept_return_character() ||
      event.GetCharacter() != ui::VKEY_RETURN) {
    // Send a blink::WebInputEvent::Char event to |host_|.
    ForwardKeyboardEventWithLatencyInfo(
        NativeWebKeyboardEvent(event, event.GetCharacter()), *event.latency(),
        nullptr);
  }
}

ui::TextInputType RenderWidgetHostViewAura::GetTextInputType() const {
  if (text_input_manager_ && text_input_manager_->GetTextInputState())
    return text_input_manager_->GetTextInputState()->type;
  return ui::TEXT_INPUT_TYPE_NONE;
}

ui::TextInputMode RenderWidgetHostViewAura::GetTextInputMode() const {
  if (text_input_manager_ && text_input_manager_->GetTextInputState())
    return text_input_manager_->GetTextInputState()->mode;
  return ui::TEXT_INPUT_MODE_DEFAULT;
}

base::i18n::TextDirection RenderWidgetHostViewAura::GetTextDirection() const {
  NOTIMPLEMENTED_LOG_ONCE();
  return base::i18n::UNKNOWN_DIRECTION;
}

int RenderWidgetHostViewAura::GetTextInputFlags() const {
  if (text_input_manager_ && text_input_manager_->GetTextInputState())
    return text_input_manager_->GetTextInputState()->flags;
  return 0;
}

bool RenderWidgetHostViewAura::CanComposeInline() const {
  if (text_input_manager_ && text_input_manager_->GetTextInputState())
    return text_input_manager_->GetTextInputState()->can_compose_inline;
  return true;
}

gfx::Rect RenderWidgetHostViewAura::ConvertRectToScreen(
    const gfx::Rect& rect) const {
  gfx::Point origin = rect.origin();
  gfx::Point end = gfx::Point(rect.right(), rect.bottom());

  aura::Window* root_window = window_->GetRootWindow();
  if (!root_window)
    return rect;
  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(root_window);
  if (!screen_position_client)
    return rect;
  screen_position_client->ConvertPointToScreen(window_, &origin);
  screen_position_client->ConvertPointToScreen(window_, &end);
  return gfx::Rect(origin.x(),
                   origin.y(),
                   end.x() - origin.x(),
                   end.y() - origin.y());
}

gfx::Rect RenderWidgetHostViewAura::ConvertRectFromScreen(
    const gfx::Rect& rect) const {
  gfx::Rect result = rect;
  if (window_->GetRootWindow() &&
      aura::client::GetScreenPositionClient(window_->GetRootWindow()))
    wm::ConvertRectFromScreen(window_, &result);
  return result;
}

gfx::Rect RenderWidgetHostViewAura::GetCaretBounds() const {
  if (!text_input_manager_ || !text_input_manager_->GetActiveWidget())
    return gfx::Rect();

  const TextInputManager::SelectionRegion* region =
      text_input_manager_->GetSelectionRegion();
  return ConvertRectToScreen(
      gfx::RectBetweenSelectionBounds(region->anchor, region->focus));
}

bool RenderWidgetHostViewAura::GetCompositionCharacterBounds(
    uint32_t index,
    gfx::Rect* rect) const {
  DCHECK(rect);

  if (!text_input_manager_ || !text_input_manager_->GetActiveWidget())
    return false;

  const TextInputManager::CompositionRangeInfo* composition_range_info =
      text_input_manager_->GetCompositionRangeInfo();

  if (index >= composition_range_info->character_bounds.size())
    return false;
  *rect = ConvertRectToScreen(composition_range_info->character_bounds[index]);
  return true;
}

bool RenderWidgetHostViewAura::HasCompositionText() const {
  return has_composition_text_;
}

ui::TextInputClient::FocusReason RenderWidgetHostViewAura::GetFocusReason()
    const {
  if (!window_->HasFocus())
    return ui::TextInputClient::FOCUS_REASON_NONE;

  switch (last_pointer_type_before_focus_) {
    case ui::EventPointerType::POINTER_TYPE_MOUSE:
      return ui::TextInputClient::FOCUS_REASON_MOUSE;
    case ui::EventPointerType::POINTER_TYPE_PEN:
      return ui::TextInputClient::FOCUS_REASON_PEN;
    case ui::EventPointerType::POINTER_TYPE_TOUCH:
      return ui::TextInputClient::FOCUS_REASON_TOUCH;
    default:
      return ui::TextInputClient::FOCUS_REASON_OTHER;
  }
}

bool RenderWidgetHostViewAura::GetTextRange(gfx::Range* range) const {
  if (!text_input_manager_ || !GetFocusedWidget())
    return false;

  const TextInputState* state = text_input_manager_->GetTextInputState();
  if (!state)
    return false;

  range->set_start(0);
  range->set_end(state->value.length());
  return true;
}

bool RenderWidgetHostViewAura::GetCompositionTextRange(
    gfx::Range* range) const {
  if (!text_input_manager_ || !GetFocusedWidget())
    return false;

  const TextInputState* state = text_input_manager_->GetTextInputState();
  // Return false when there is no composition.
  if (!state || state->composition_start == -1)
    return false;

  range->set_start(state->composition_start);
  range->set_end(state->composition_end);
  return true;
}

bool RenderWidgetHostViewAura::GetEditableSelectionRange(
    gfx::Range* range) const {
  if (!text_input_manager_ || !GetFocusedWidget())
    return false;

  const TextInputState* state = text_input_manager_->GetTextInputState();
  if (!state)
    return false;

  range->set_start(state->selection_start);
  range->set_end(state->selection_end);
  return true;
}

bool RenderWidgetHostViewAura::SetEditableSelectionRange(
    const gfx::Range& range) {
  // TODO(crbug.com/915630): Write an unit test for this method.
  RenderFrameHostImpl* rfh = GetFocusedFrame();
  if (!rfh)
    return false;
  auto* input_handler = rfh->GetFrameInputHandler();
  if (!input_handler)
    return false;
  input_handler->SetEditableSelectionOffsets(range.start(), range.end());
  return true;
}

bool RenderWidgetHostViewAura::DeleteRange(const gfx::Range& range) {
  // TODO(suzhe): implement this method when fixing http://crbug.com/55130.
  NOTIMPLEMENTED_LOG_ONCE();
  return false;
}

bool RenderWidgetHostViewAura::GetTextFromRange(
    const gfx::Range& range,
    base::string16* text) const {
  if (!text_input_manager_ || !GetFocusedWidget())
    return false;

  const TextInputState* state = text_input_manager_->GetTextInputState();
  if (!state)
    return false;

  gfx::Range text_range;
  GetTextRange(&text_range);

  if (!text_range.Contains(range)) {
    text->clear();
    return false;
  }
  if (text_range.EqualsIgnoringDirection(range)) {
    // Avoid calling substr whose performance is low.
    *text = state->value;
  } else {
    *text = state->value.substr(range.GetMin(), range.length());
  }
  return true;
}

void RenderWidgetHostViewAura::OnInputMethodChanged() {
  // TODO(suzhe): implement the newly added "locale" property of HTML DOM
  // TextEvent.
}

bool RenderWidgetHostViewAura::ChangeTextDirectionAndLayoutAlignment(
      base::i18n::TextDirection direction) {
  if (!GetTextInputManager() && !GetTextInputManager()->GetActiveWidget())
    return false;

  GetTextInputManager()->GetActiveWidget()->UpdateTextDirection(
      direction == base::i18n::RIGHT_TO_LEFT
          ? blink::kWebTextDirectionRightToLeft
          : blink::kWebTextDirectionLeftToRight);
  GetTextInputManager()->GetActiveWidget()->NotifyTextDirection();
  return true;
}

void RenderWidgetHostViewAura::ExtendSelectionAndDelete(
    size_t before, size_t after) {
  auto* input_handler = GetFrameInputHandlerForFocusedFrame(host());
  if (!input_handler)
    return;
  input_handler->ExtendSelectionAndDelete(before, after);
}

void RenderWidgetHostViewAura::EnsureCaretNotInRect(
    const gfx::Rect& rect_in_screen) {
  aura::Window* top_level_window = window_->GetToplevelWindow();
#if defined(OS_CHROMEOS)
  wm::EnsureWindowNotInRect(top_level_window, rect_in_screen);
#endif

  // Perform overscroll if the caret is still hidden by the keyboard.
  const gfx::Rect hidden_window_bounds_in_screen = gfx::IntersectRects(
      rect_in_screen, top_level_window->GetBoundsInScreen());
  if (hidden_window_bounds_in_screen.IsEmpty())
    return;

  gfx::Rect visible_area_in_local_space = gfx::SubtractRects(
      window_->GetBoundsInScreen(), hidden_window_bounds_in_screen);
  visible_area_in_local_space =
      ConvertRectFromScreen(visible_area_in_local_space);
  ScrollFocusedEditableNodeIntoRect(visible_area_in_local_space);
}

bool RenderWidgetHostViewAura::IsTextEditCommandEnabled(
    ui::TextEditCommand command) const {
  return false;
}

void RenderWidgetHostViewAura::SetTextEditCommandForNextKeyEvent(
    ui::TextEditCommand command) {}

ukm::SourceId RenderWidgetHostViewAura::GetClientSourceForMetrics() const {
  RenderFrameHostImpl* frame = GetFocusedFrame();
  if (frame) {
    return frame->GetRenderWidgetHost()
        ->delegate()
        ->GetUkmSourceIdForLastCommittedSource();
  }
  return ukm::SourceId();
}

bool RenderWidgetHostViewAura::ShouldDoLearning() {
  return GetTextInputManager() && GetTextInputManager()->should_do_learning();
}

#if defined(OS_WIN) || defined(OS_CHROMEOS)
bool RenderWidgetHostViewAura::SetCompositionFromExistingText(
    const gfx::Range& range,
    const std::vector<ui::ImeTextSpan>& ui_ime_text_spans) {
  auto* input_handler = GetFrameInputHandlerForFocusedFrame(host());
  if (!input_handler)
    return false;
  input_handler->SetCompositionFromExistingText(range.start(), range.end(),
                                                ui_ime_text_spans);
  has_composition_text_ = true;
  return true;
}

#endif

#if defined(OS_WIN)
void RenderWidgetHostViewAura::SetActiveCompositionForAccessibility(
    const gfx::Range& range,
    const base::string16& active_composition_text,
    bool is_composition_committed) {
  BrowserAccessibilityManager* manager =
      host()->GetRootBrowserAccessibilityManager();
  if (manager) {
    ui::AXPlatformNodeWin* focus_node = static_cast<ui::AXPlatformNodeWin*>(
        ui::AXPlatformNode::FromNativeViewAccessible(
            manager->GetFocus()->GetNativeViewAccessible()));
    if (focus_node) {
      // Notify accessibility object about this composition
      focus_node->OnActiveComposition(range, active_composition_text,
                                      is_composition_committed);
    }
  }
}

#endif

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, display::DisplayObserver implementation:

void RenderWidgetHostViewAura::OnDisplayMetricsChanged(
    const display::Display& display,
    uint32_t metrics) {
  display::Screen* screen = display::Screen::GetScreen();
  if (display.id() != screen->GetDisplayNearestWindow(window_).id())
    return;

  if (window_->GetHost() && window_->GetHost()->device_scale_factor() !=
                                display.device_scale_factor()) {
    // The DisplayMetrics changed, but the Compositor hasn't been updated yet.
    // Delay updating until the Compositor is updated as well, otherwise we
    // are likely to hit surface invariants (LocalSurfaceId generated with a
    // size/scale-factor that differs from scale-factor used by Compositor).
    needs_to_update_display_metrics_ = true;
    return;
  }
  ProcessDisplayMetricsChanged();
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, aura::WindowDelegate implementation:

gfx::Size RenderWidgetHostViewAura::GetMinimumSize() const {
  return gfx::Size();
}

gfx::Size RenderWidgetHostViewAura::GetMaximumSize() const {
  return gfx::Size();
}

void RenderWidgetHostViewAura::OnBoundsChanged(const gfx::Rect& old_bounds,
                                               const gfx::Rect& new_bounds) {
  base::AutoReset<bool> in_bounds_changed(&in_bounds_changed_, true);
  // We care about this whenever RenderWidgetHostViewAura is not owned by a
  // WebContentsViewAura since changes to the Window's bounds need to be
  // messaged to the renderer.  WebContentsViewAura invokes SetSize() or
  // SetBounds() itself.  No matter how we got here, any redundant calls are
  // harmless.
  SetSize(new_bounds.size());

  if (GetInputMethod())
    GetInputMethod()->OnCaretBoundsChanged(this);
}

gfx::NativeCursor RenderWidgetHostViewAura::GetCursor(const gfx::Point& point) {
  if (IsMouseLocked())
    return ui::CursorType::kNone;
  return current_cursor_.GetNativeCursor();
}

int RenderWidgetHostViewAura::GetNonClientComponent(
    const gfx::Point& point) const {
  return HTCLIENT;
}

bool RenderWidgetHostViewAura::ShouldDescendIntoChildForEventHandling(
    aura::Window* child,
    const gfx::Point& location) {
  return true;
}

bool RenderWidgetHostViewAura::CanFocus() {
  return widget_type_ == WidgetType::kFrame;
}

void RenderWidgetHostViewAura::OnCaptureLost() {
  host()->LostCapture();
}

void RenderWidgetHostViewAura::OnPaint(const ui::PaintContext& context) {
  NOTREACHED();
}

void RenderWidgetHostViewAura::OnDeviceScaleFactorChanged(
    float old_device_scale_factor,
    float new_device_scale_factor) {
  if (!window_->GetRootWindow())
    return;

  if (needs_to_update_display_metrics_)
    ProcessDisplayMetricsChanged();

  SynchronizeVisualProperties(cc::DeadlinePolicy::UseDefaultDeadline(),
                              window_->GetLocalSurfaceIdAllocation());

  device_scale_factor_ = new_device_scale_factor;
  const display::Display display =
      display::Screen::GetScreen()->GetDisplayNearestWindow(window_);
  DCHECK_EQ(new_device_scale_factor, display.device_scale_factor());
  current_cursor_.SetDisplayInfo(display);
}

void RenderWidgetHostViewAura::OnWindowDestroying(aura::Window* window) {
#if defined(OS_WIN)
  // The LegacyRenderWidgetHostHWND instance is destroyed when its window is
  // destroyed. Normally we control when that happens via the Destroy call
  // in the dtor. However there may be cases where the window is destroyed
  // by Windows, i.e. the parent window is destroyed before the
  // RenderWidgetHostViewAura instance goes away etc. To avoid that we
  // destroy the LegacyRenderWidgetHostHWND instance here.
  if (legacy_render_widget_host_HWND_) {
    legacy_render_widget_host_HWND_->Destroy();
    // The Destroy call above will delete the LegacyRenderWidgetHostHWND
    // instance.
    legacy_render_widget_host_HWND_ = NULL;
  }
#endif

  // Make sure that the input method no longer references to this object before
  // this object is removed from the root window (i.e. this object loses access
  // to the input method).
  DetachFromInputMethod();

  if (overscroll_controller_)
    overscroll_controller_->Reset();
}

void RenderWidgetHostViewAura::OnWindowDestroyed(aura::Window* window) {
  // This is not called on all destruction paths (e.g. if this view was never
  // inialized properly to create the window). So the destruction/cleanup code
  // that do not depend on |window_| should happen in the destructor, not here.
  delete this;
}

void RenderWidgetHostViewAura::OnWindowTargetVisibilityChanged(bool visible) {
}

bool RenderWidgetHostViewAura::HasHitTestMask() const {
  return false;
}

void RenderWidgetHostViewAura::GetHitTestMask(SkPath* mask) const {}

bool RenderWidgetHostViewAura::RequiresDoubleTapGestureEvents() const {
  RenderWidgetHostOwnerDelegate* owner_delegate = host()->owner_delegate();
  // TODO(crbug.com/916715): Child local roots do not work here?
  if (!owner_delegate)
    return false;
  return owner_delegate->GetWebkitPreferencesForWidget()
      .double_tap_to_zoom_enabled;
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, ui::EventHandler implementation:

void RenderWidgetHostViewAura::OnKeyEvent(ui::KeyEvent* event) {
  last_pointer_type_ = ui::EventPointerType::POINTER_TYPE_UNKNOWN;
  event_handler_->OnKeyEvent(event);
}

void RenderWidgetHostViewAura::OnMouseEvent(ui::MouseEvent* event) {
#if defined(OS_WIN)
  if (event->type() == ui::ET_MOUSE_MOVED) {
    if (event->location() == last_mouse_move_location_ &&
        event->movement().IsZero()) {
      event->SetHandled();
      return;
    }
    last_mouse_move_location_ = event->location();
  }
#endif
  last_pointer_type_ = ui::EventPointerType::POINTER_TYPE_MOUSE;
  event_handler_->OnMouseEvent(event);
}

bool RenderWidgetHostViewAura::HasFallbackSurface() const {
  return delegated_frame_host_ && delegated_frame_host_->HasFallbackSurface();
}

bool RenderWidgetHostViewAura::TransformPointToCoordSpaceForView(
    const gfx::PointF& point,
    RenderWidgetHostViewBase* target_view,
    gfx::PointF* transformed_point) {
  if (target_view == this || !delegated_frame_host_) {
    *transformed_point = point;
    return true;
  }

  // In TransformPointToLocalCoordSpace() there is a Point-to-Pixel conversion,
  // but it is not necessary here because the final target view is responsible
  // for converting before computing the final transform.
  return target_view->TransformPointToLocalCoordSpace(
      point, GetCurrentSurfaceId(), transformed_point);
}

viz::FrameSinkId RenderWidgetHostViewAura::GetRootFrameSinkId() {
  if (!window_ || !window_->GetHost() || !window_->GetHost()->compositor())
    return viz::FrameSinkId();

  return window_->GetHost()->compositor()->frame_sink_id();
}

viz::SurfaceId RenderWidgetHostViewAura::GetCurrentSurfaceId() const {
  return delegated_frame_host_ ? delegated_frame_host_->GetCurrentSurfaceId()
                               : viz::SurfaceId();
}

void RenderWidgetHostViewAura::FocusedNodeChanged(
    bool editable,
    const gfx::Rect& node_bounds_in_screen) {
  // The last gesture most likely caused the focus change. The focus reason will
  // be incorrect if the focus was triggered without a user gesture.
  // TODO(https://crbug.com/824604): Get the focus reason from the renderer
  // process instead to get the true focus reason.
  last_pointer_type_before_focus_ = last_pointer_type_;

  auto* input_method = GetInputMethod();
  if (input_method)
    input_method->CancelComposition(this);
  has_composition_text_ = false;

#if defined(OS_WIN)
  if (!editable && virtual_keyboard_requested_ && window_) {
    virtual_keyboard_requested_ = false;

    if (input_method && input_method->GetInputMethodKeyboardController()) {
      input_method->GetInputMethodKeyboardController()
          ->DismissVirtualKeyboard();
    }
  }
#elif defined(OS_FUCHSIA)
  if (!editable && window_) {
    if (input_method) {
      input_method->GetInputMethodKeyboardController()
          ->DismissVirtualKeyboard();
    }
  }
#endif
}

void RenderWidgetHostViewAura::OnScrollEvent(ui::ScrollEvent* event) {
  event_handler_->OnScrollEvent(event);
}

void RenderWidgetHostViewAura::OnTouchEvent(ui::TouchEvent* event) {
  last_pointer_type_ = event->pointer_details().pointer_type;
  event_handler_->OnTouchEvent(event);
}

void RenderWidgetHostViewAura::OnGestureEvent(ui::GestureEvent* event) {
  last_pointer_type_ = event->details().primary_pointer_type();
  event_handler_->OnGestureEvent(event);
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, wm::ActivationDelegate implementation:

bool RenderWidgetHostViewAura::ShouldActivate() const {
  aura::WindowTreeHost* host = window_->GetHost();
  if (!host)
    return true;
  const ui::Event* event = host->dispatcher()->current_event();
  if (!event)
    return true;
  return is_fullscreen_;
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, aura::client::CursorClientObserver implementation:

void RenderWidgetHostViewAura::OnCursorVisibilityChanged(bool is_visible) {
  NotifyRendererOfCursorVisibilityState(is_visible);
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, aura::client::FocusChangeObserver implementation:

void RenderWidgetHostViewAura::OnWindowFocused(aura::Window* gained_focus,
                                               aura::Window* lost_focus) {
  if (window_ == gained_focus) {
    // We need to honor input bypass if the associated tab is does not want
    // input. This gives the current focused window a chance to be the text
    // input client and handle events.
    if (host()->IsIgnoringInputEvents())
      return;

    host()->GotFocus();
    host()->SetActive(true);

    ui::InputMethod* input_method = GetInputMethod();
    if (input_method) {
      // Ask the system-wide IME to send all TextInputClient messages to |this|
      // object.
      input_method->SetFocusedTextInputClient(this);
    }

    BrowserAccessibilityManager* manager =
        host()->GetRootBrowserAccessibilityManager();
    if (manager)
      manager->OnWindowFocused();
    return;
  }

  if (window_ != lost_focus) {
    NOTREACHED();
    return;
  }

  host()->SetActive(false);
  host()->LostFocus();

  DetachFromInputMethod();

  // TODO(wjmaclean): Do we need to let TouchSelectionControllerClientAura
  // handle this, just in case it stomps on a new highlight in another view
  // that has just become focused? So far it doesn't appear to be a problem,
  // but we should keep an eye on it.
  selection_controller_->HideAndDisallowShowingAutomatically();

  if (overscroll_controller_)
    overscroll_controller_->Cancel();

  BrowserAccessibilityManager* manager =
      host()->GetRootBrowserAccessibilityManager();
  if (manager)
    manager->OnWindowBlurred();

  // If we lose the focus while fullscreen, close the window; Pepper Flash
  // won't do it for us (unlike NPAPI Flash). However, we do not close the
  // window if we lose the focus to a window on another display.
  display::Screen* screen = display::Screen::GetScreen();
  bool focusing_other_display =
      gained_focus && screen->GetNumDisplays() > 1 &&
      (screen->GetDisplayNearestWindow(window_).id() !=
       screen->GetDisplayNearestWindow(gained_focus).id());
  if (is_fullscreen_ && !in_shutdown_ && !focusing_other_display) {
#if defined(OS_WIN)
    // On Windows, if we are switching to a non Aura Window on a different
    // screen we should not close the fullscreen window.
    if (!gained_focus) {
      POINT point = {0};
      ::GetCursorPos(&point);
      if (screen->GetDisplayNearestWindow(window_).id() !=
          screen->GetDisplayNearestPoint(gfx::Point(point)).id()) {
        return;
      }
    }
#endif
    Shutdown();
    return;
  }

  // Close the child popup window if we lose focus (e.g. due to a JS alert or
  // system modal dialog). This is particularly important if
  // |popup_child_host_view_| has mouse capture.
  if (popup_child_host_view_)
    popup_child_host_view_->Shutdown();
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, aura::WindowTreeHostObserver implementation:

void RenderWidgetHostViewAura::OnHostMovedInPixels(
    aura::WindowTreeHost* host,
    const gfx::Point& new_origin_in_pixels) {
  TRACE_EVENT1("ui", "RenderWidgetHostViewAura::OnHostMovedInPixels",
               "new_origin_in_pixels", new_origin_in_pixels.ToString());

  UpdateScreenInfo(window_);
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, RenderFrameMetadataProvider::Observer
// implementation:
void RenderWidgetHostViewAura::OnRenderFrameMetadataChangedAfterActivation() {
  RenderWidgetHostViewBase::OnRenderFrameMetadataChangedAfterActivation();
  const cc::RenderFrameMetadata& metadata =
      host()->render_frame_metadata_provider()->LastRenderFrameMetadata();
  SetContentBackgroundColor(metadata.root_background_color);
  if (inset_surface_id_allocation_.IsValid() &&
      metadata.local_surface_id_allocation &&
      metadata.local_surface_id_allocation.value().IsValid() &&
      metadata.local_surface_id_allocation.value()
          .local_surface_id()
          .IsSameOrNewerThan(inset_surface_id_allocation_.local_surface_id())) {
    inset_surface_id_allocation_ = viz::LocalSurfaceIdAllocation();
    ScrollFocusedEditableNodeIntoRect(gfx::Rect());
  }

  if (metadata.selection.start != selection_start_ ||
      metadata.selection.end != selection_end_) {
    selection_start_ = metadata.selection.start;
    selection_end_ = metadata.selection.end;
    selection_controller_client_->UpdateClientSelectionBounds(selection_start_,
                                                              selection_end_);
  }
}

////////////////////////////////////////////////////////////////////////////////
// RenderWidgetHostViewAura, private:

RenderWidgetHostViewAura::~RenderWidgetHostViewAura() {
  // Ask the RWH to drop reference to us.
  if (!is_guest_view_hack_)
    host()->ViewDestroyed();

  selection_controller_.reset();
  selection_controller_client_.reset();

  GetCursorManager()->ViewBeingDestroyed(this);

  delegated_frame_host_.reset();
  window_observer_.reset();
  if (window_) {
    if (window_->GetHost())
      window_->GetHost()->RemoveObserver(this);
    UnlockMouse();
    wm::SetTooltipText(window_, nullptr);
    display::Screen::GetScreen()->RemoveObserver(this);

    // This call is usually no-op since |this| object is already removed from
    // the Aura root window and we don't have a way to get an input method
    // object associated with the window, but just in case.
    DetachFromInputMethod();
  }
  if (popup_parent_host_view_) {
    DCHECK(!popup_parent_host_view_->popup_child_host_view_ ||
           popup_parent_host_view_->popup_child_host_view_ == this);
    popup_parent_host_view_->SetPopupChild(nullptr);
  }
  if (popup_child_host_view_) {
    DCHECK(!popup_child_host_view_->popup_parent_host_view_ ||
           popup_child_host_view_->popup_parent_host_view_ == this);
    popup_child_host_view_->popup_parent_host_view_ = nullptr;
  }
  event_observer_for_popup_exit_.reset();

#if defined(OS_WIN)
  // The LegacyRenderWidgetHostHWND window should have been destroyed in
  // RenderWidgetHostViewAura::OnWindowDestroying and the pointer should
  // be set to NULL.
  DCHECK(!legacy_render_widget_host_HWND_);
#endif

  if (text_input_manager_)
    text_input_manager_->RemoveObserver(this);
}

void RenderWidgetHostViewAura::CreateAuraWindow(aura::client::WindowType type) {
  DCHECK(!window_);
  window_ = new aura::Window(this);
  window_->SetName("RenderWidgetHostViewAura");
  event_handler_->set_window(window_);
  window_observer_.reset(new WindowObserver(this));

  wm::SetTooltipText(window_, &tooltip_);
  wm::SetActivationDelegate(window_, this);
  aura::client::SetFocusChangeObserver(window_, this);
  display::Screen::GetScreen()->AddObserver(this);

  window_->SetType(type);
  window_->Init(ui::LAYER_SOLID_COLOR);
  window_->layer()->SetColor(GetBackgroundColor() ? *GetBackgroundColor()
                                                  : SK_ColorWHITE);
  // This needs to happen only after |window_| has been initialized using
  // Init(), because it needs to have the layer.
  if (frame_sink_id_.is_valid())
    window_->SetEmbedFrameSinkId(frame_sink_id_);
}

void RenderWidgetHostViewAura::CreateDelegatedFrameHostClient() {
  if (!frame_sink_id_.is_valid())
    return;

  delegated_frame_host_client_ =
      std::make_unique<DelegatedFrameHostClientAura>(this);
  delegated_frame_host_ = std::make_unique<DelegatedFrameHost>(
      frame_sink_id_, delegated_frame_host_client_.get(),
      false /* should_register_frame_sink_id */);

  // Let the page-level input event router know about our surface ID
  // namespace for surface-based hit testing.
  if (host()->delegate() && host()->delegate()->GetInputEventRouter()) {
    host()->delegate()->GetInputEventRouter()->AddFrameSinkIdOwner(
        GetFrameSinkId(), this);
  }
}

void RenderWidgetHostViewAura::UpdateCursorIfOverSelf() {
  if (host()->GetProcess()->FastShutdownStarted())
    return;

  aura::Window* root_window = window_->GetRootWindow();
  if (!root_window)
    return;

  display::Screen* screen = display::Screen::GetScreen();
  DCHECK(screen);

  gfx::Point cursor_screen_point = screen->GetCursorScreenPoint();

#if !defined(OS_CHROMEOS)
  // Ignore cursor update messages if the window under the cursor is not us.
  aura::Window* window_at_screen_point = screen->GetWindowAtScreenPoint(
      cursor_screen_point);
#if defined(OS_WIN)
  // On Windows we may fail to retrieve the aura Window at the current cursor
  // position. This is because the WindowFromPoint API may return the legacy
  // window which is not associated with an aura Window. In this case we need
  // to get the aura window for the parent of the legacy window.
  if (!window_at_screen_point && legacy_render_widget_host_HWND_) {
    HWND hwnd_at_point = ::WindowFromPoint(cursor_screen_point.ToPOINT());

    if (hwnd_at_point == legacy_render_widget_host_HWND_->hwnd())
      hwnd_at_point = legacy_render_widget_host_HWND_->GetParent();

    display::win::ScreenWin* screen_win =
        static_cast<display::win::ScreenWin*>(screen);
    window_at_screen_point = screen_win->GetNativeWindowFromHWND(
        hwnd_at_point);
  }
#endif  // defined(OS_WIN)
  if (!window_at_screen_point ||
      (window_at_screen_point->GetRootWindow() != root_window)) {
    return;
  }
#endif  // !defined(OS_CHROMEOS)

  gfx::Point root_window_point = cursor_screen_point;
  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(root_window);
  if (screen_position_client) {
    screen_position_client->ConvertPointFromScreen(
        root_window, &root_window_point);
  }

  if (root_window->GetEventHandlerForPoint(root_window_point) != window_)
    return;

  gfx::NativeCursor cursor = current_cursor_.GetNativeCursor();
  // Do not show loading cursor when the cursor is currently hidden.
  if (is_loading_ && cursor != ui::CursorType::kNone)
    cursor = ui::Cursor(ui::CursorType::kPointer);

  aura::client::CursorClient* cursor_client =
      aura::client::GetCursorClient(root_window);
  if (cursor_client) {
    cursor_client->SetCursor(cursor);
  }
}

bool RenderWidgetHostViewAura::SynchronizeVisualProperties(
    const cc::DeadlinePolicy& deadline_policy,
    const base::Optional<viz::LocalSurfaceIdAllocation>&
        child_local_surface_id_allocation) {
  DCHECK(window_);
  window_->UpdateLocalSurfaceIdFromEmbeddedClient(
      child_local_surface_id_allocation);
  // If the viz::LocalSurfaceIdAllocation is invalid, we may have been evicted,
  // allocate a new one to establish bounds.
  if (!GetLocalSurfaceIdAllocation().IsValid())
    window_->AllocateLocalSurfaceId();

  if (delegated_frame_host_) {
    delegated_frame_host_->EmbedSurface(
        GetLocalSurfaceIdAllocation().local_surface_id(),
        window_->bounds().size(), deadline_policy);
  }
  return host()->SynchronizeVisualProperties();
}

void RenderWidgetHostViewAura::OnDidUpdateVisualPropertiesComplete(
    const cc::RenderFrameMetadata& metadata) {
  DCHECK(window_);

  if (host()->delegate()) {
    host()->delegate()->SetTopControlsShownRatio(
        host(), metadata.top_controls_shown_ratio);
  }

  if (host()->is_hidden()) {
    // When an embedded child responds, we want to accept its changes to the
    // viz::LocalSurfaceId. However we do not want to embed surfaces while
    // hidden. Nor do we want to embed invalid ids when we are evicted. Becoming
    // visible will generate a new id, if necessary, and begin embedding.
    window_->UpdateLocalSurfaceIdFromEmbeddedClient(
        metadata.local_surface_id_allocation);
  } else {
    SynchronizeVisualProperties(cc::DeadlinePolicy::UseDefaultDeadline(),
                                metadata.local_surface_id_allocation);
  }
}

ui::InputMethod* RenderWidgetHostViewAura::GetInputMethod() const {
  if (!window_)
    return nullptr;
  aura::Window* root_window = window_->GetRootWindow();
  if (!root_window)
    return nullptr;
  return root_window->GetHost()->GetInputMethod();
}

RenderWidgetHostViewBase*
RenderWidgetHostViewAura::GetFocusedViewForTextSelection() {
  // We obtain the TextSelection from focused RWH which is obtained from the
  // frame tree. BrowserPlugin-based guests' RWH is not part of the frame tree
  // and the focused RWH will be that of the embedder which is incorrect. In
  // this case we should use TextSelection for |this| since RWHV for guest
  // forwards text selection information to its platform view.
  return is_guest_view_hack_
             ? this
             : GetFocusedWidget() ? GetFocusedWidget()->GetView() : nullptr;
}

void RenderWidgetHostViewAura::Shutdown() {
  if (!in_shutdown_) {
    in_shutdown_ = true;
    host()->ShutdownAndDestroyWidget(true);
  }
}

TouchSelectionControllerClientManager*
RenderWidgetHostViewAura::GetTouchSelectionControllerClientManager() {
  return selection_controller_client_.get();
}

bool RenderWidgetHostViewAura::NeedsInputGrab() {
  return widget_type_ == WidgetType::kPopup;
}

bool RenderWidgetHostViewAura::NeedsMouseCapture() {
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  return NeedsInputGrab();
#endif
  return false;
}

void RenderWidgetHostViewAura::SetTooltipsEnabled(bool enable) {
  if (enable) {
    tooltip_disabler_.reset();
  } else {
    tooltip_disabler_.reset(
        new wm::ScopedTooltipDisabler(window_->GetRootWindow()));
  }
}

void RenderWidgetHostViewAura::ShowContextMenu(
    const ContextMenuParams& params) {
  // Use RenderViewHostDelegate to get to the WebContentsViewAura, which will
  // actually show the disambiguation popup.
  // NOTE: This only works for main frame widgets then, as child frame widgets
  // don't have an owner delegate and won't get access to the RenderViewHost
  // here.
  RenderWidgetHostOwnerDelegate* owner_delegate = host()->owner_delegate();
  if (owner_delegate)
    owner_delegate->ShowContextMenu(GetFocusedFrame(), params);
}

void RenderWidgetHostViewAura::NotifyRendererOfCursorVisibilityState(
    bool is_visible) {
  if (host()->is_hidden() ||
      (cursor_visibility_state_in_renderer_ == VISIBLE && is_visible) ||
      (cursor_visibility_state_in_renderer_ == NOT_VISIBLE && !is_visible))
    return;

  cursor_visibility_state_in_renderer_ = is_visible ? VISIBLE : NOT_VISIBLE;
  host()->OnCursorVisibilityStateChanged(is_visible);
}

void RenderWidgetHostViewAura::SetOverscrollControllerEnabled(bool enabled) {
  if (!enabled)
    overscroll_controller_.reset();
  else if (!overscroll_controller_)
    overscroll_controller_.reset(new OverscrollController());
}

void RenderWidgetHostViewAura::SetOverscrollControllerForTesting(
    std::unique_ptr<OverscrollController> controller) {
  overscroll_controller_ = std::move(controller);
}

void RenderWidgetHostViewAura::SetSelectionControllerClientForTest(
    std::unique_ptr<TouchSelectionControllerClientAura> client) {
  selection_controller_client_.swap(client);
  CreateSelectionController();
}

void RenderWidgetHostViewAura::InternalSetBounds(const gfx::Rect& rect) {
  // Don't recursively call SetBounds if this bounds update is the result of
  // a Window::SetBoundsInternal call.
  if (!in_bounds_changed_)
    window_->SetBounds(rect);

  // Even if not showing yet, we need to synchronize on size. As the renderer
  // needs to begin layout. Waiting until we show to start layout leads to
  // significant delays in embedding the first shown surface (500+ ms.)
  SynchronizeVisualProperties(cc::DeadlinePolicy::UseDefaultDeadline(),
                              window_->GetLocalSurfaceIdAllocation());

#if defined(OS_WIN)
  UpdateLegacyWin();

  if (IsMouseLocked())
    UpdateMouseLockRegion();
#endif
}

#if defined(OS_WIN)
void RenderWidgetHostViewAura::UpdateLegacyWin() {
  if (legacy_window_destroyed_ || !GetHostWindowHWND())
    return;

  if (!legacy_render_widget_host_HWND_) {
    legacy_render_widget_host_HWND_ =
        LegacyRenderWidgetHostHWND::Create(GetHostWindowHWND());
  }

  if (legacy_render_widget_host_HWND_) {
    legacy_render_widget_host_HWND_->set_host(this);
    legacy_render_widget_host_HWND_->UpdateParent(GetHostWindowHWND());
    legacy_render_widget_host_HWND_->SetBounds(
        window_->GetBoundsInRootWindow());
    // There are cases where the parent window is created, made visible and
    // the associated RenderWidget is also visible before the
    // LegacyRenderWidgetHostHWND instace is created. Ensure that it is shown
    // here.
    if (!host()->is_hidden())
      legacy_render_widget_host_HWND_->Show();
  }
}
#endif

void RenderWidgetHostViewAura::AddedToRootWindow() {
  window_->GetHost()->AddObserver(this);
  UpdateScreenInfo(window_);

  aura::client::CursorClient* cursor_client =
      aura::client::GetCursorClient(window_->GetRootWindow());
  if (cursor_client) {
    cursor_client->AddObserver(this);
    NotifyRendererOfCursorVisibilityState(cursor_client->IsCursorVisible());
  }
  if (HasFocus()) {
    ui::InputMethod* input_method = GetInputMethod();
    if (input_method)
      input_method->SetFocusedTextInputClient(this);
  }

#if defined(OS_WIN)
  UpdateLegacyWin();
#endif

  if (delegated_frame_host_)
    delegated_frame_host_->AttachToCompositor(window_->GetHost()->compositor());
}

void RenderWidgetHostViewAura::RemovingFromRootWindow() {
  aura::client::CursorClient* cursor_client =
      aura::client::GetCursorClient(window_->GetRootWindow());
  if (cursor_client)
    cursor_client->RemoveObserver(this);

  DetachFromInputMethod();

  window_->GetHost()->RemoveObserver(this);
  if (delegated_frame_host_)
    delegated_frame_host_->DetachFromCompositor();

#if defined(OS_WIN)
  // Update the legacy window's parent temporarily to the hidden window. It
  // will eventually get reparented to the right root.
  if (legacy_render_widget_host_HWND_)
    legacy_render_widget_host_HWND_->UpdateParent(ui::GetHiddenWindow());
#endif
}

void RenderWidgetHostViewAura::DetachFromInputMethod() {
  ui::InputMethod* input_method = GetInputMethod();
  if (input_method) {
    input_method->DetachTextInputClient(this);
#if defined(OS_CHROMEOS)
    wm::RestoreWindowBoundsOnClientFocusLost(window_->GetToplevelWindow());
#endif  // defined(OS_CHROMEOS)
  }

#if defined(OS_WIN)
  // Reset the keyboard observer because it attaches to the input method.
  keyboard_observer_.reset();
#endif  // defined(OS_WIN)
}

void RenderWidgetHostViewAura::ForwardKeyboardEventWithLatencyInfo(
    const NativeWebKeyboardEvent& event,
    const ui::LatencyInfo& latency,
    bool* update_event) {
  RenderWidgetHostImpl* target_host = host();

  // If there are multiple widgets on the page (such as when there are
  // out-of-process iframes), pick the one that should process this event.
  if (host()->delegate())
    target_host = host()->delegate()->GetFocusedRenderWidgetHost(host());
  if (!target_host)
    return;

#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  ui::TextEditKeyBindingsDelegateAuraLinux* keybinding_delegate =
      ui::GetTextEditKeyBindingsDelegate();
  std::vector<ui::TextEditCommandAuraLinux> commands;
  if (!event.skip_in_browser &&
      keybinding_delegate &&
      event.os_event &&
      keybinding_delegate->MatchEvent(*event.os_event, &commands)) {
    // Transform from ui/ types to content/ types.
    EditCommands edit_commands;
    for (std::vector<ui::TextEditCommandAuraLinux>::const_iterator it =
             commands.begin(); it != commands.end(); ++it) {
      edit_commands.push_back(EditCommand(it->GetCommandString(),
                                          it->argument()));
    }

    target_host->ForwardKeyboardEventWithCommands(event, latency,
                                                  &edit_commands, update_event);
    return;
  }
#endif

  target_host->ForwardKeyboardEventWithCommands(event, latency, nullptr,
                                                update_event);
}

void RenderWidgetHostViewAura::CreateSelectionController() {
  ui::TouchSelectionController::Config tsc_config;
  tsc_config.max_tap_duration = base::TimeDelta::FromMilliseconds(
      ui::GestureConfiguration::GetInstance()->long_press_time_in_ms());
  tsc_config.tap_slop = ui::GestureConfiguration::GetInstance()
                            ->max_touch_move_in_pixels_for_click();
  tsc_config.enable_longpress_drag_selection = false;
  selection_controller_.reset(new ui::TouchSelectionController(
      selection_controller_client_.get(), tsc_config));
}

void RenderWidgetHostViewAura::OnDidNavigateMainFrameToNewPage() {
  CancelActiveTouches();
}

const viz::FrameSinkId& RenderWidgetHostViewAura::GetFrameSinkId() const {
  return frame_sink_id_;
}

const viz::LocalSurfaceIdAllocation&
RenderWidgetHostViewAura::GetLocalSurfaceIdAllocation() const {
  return window_->GetLocalSurfaceIdAllocation();
}

void RenderWidgetHostViewAura::OnUpdateTextInputStateCalled(
    TextInputManager* text_input_manager,
    RenderWidgetHostViewBase* updated_view,
    bool did_update_state) {
  DCHECK_EQ(text_input_manager_, text_input_manager);

  if (!GetInputMethod())
    return;

  if (did_update_state)
    GetInputMethod()->OnTextInputTypeChanged(this);

  const TextInputState* state = text_input_manager_->GetTextInputState();
  if (state && state->type != ui::TEXT_INPUT_TYPE_NONE &&
      state->mode != ui::TEXT_INPUT_MODE_NONE) {
    bool show_virtual_keyboard = true;
#if defined(OS_WIN) || defined(OS_FUCHSIA)
    show_virtual_keyboard =
        last_pointer_type_ == ui::EventPointerType::POINTER_TYPE_TOUCH;
#endif
    if (state->show_ime_if_needed &&
        GetInputMethod()->GetTextInputClient() == this &&
        show_virtual_keyboard) {
      GetInputMethod()->ShowVirtualKeyboardIfEnabled();
    }
    // Ensure that accessibility events are fired when the selection location
    // moves from UI back to content.
    text_input_manager->NotifySelectionBoundsChanged(updated_view);
  }

  if (auto* render_widget_host = updated_view->host()) {
    // Monitor the composition information if there is a focused editable node.
    render_widget_host->RequestCompositionUpdates(
        false /* immediate_request */,
        state &&
            (state->type != ui::TEXT_INPUT_TYPE_NONE) /* monitor_updates */);
  }
}

void RenderWidgetHostViewAura::OnImeCancelComposition(
    TextInputManager* text_input_manager,
    RenderWidgetHostViewBase* view) {
  // |view| is not necessarily the one corresponding to
  // TextInputManager::GetActiveWidget() as RenderWidgetHostViewAura can call
  // this method to finish any ongoing composition in response to a mouse down
  // event.
  if (GetInputMethod())
    GetInputMethod()->CancelComposition(this);
  has_composition_text_ = false;
}

void RenderWidgetHostViewAura::OnSelectionBoundsChanged(
    TextInputManager* text_input_manager,
    RenderWidgetHostViewBase* updated_view) {
  // Note: accessibility caret move events are no longer fired directly here,
  // because they were redundant with the events fired by the top level window
  // by HWNDMessageHandler::OnCaretBoundsChanged().
  if (GetInputMethod())
    GetInputMethod()->OnCaretBoundsChanged(this);
}

void RenderWidgetHostViewAura::OnTextSelectionChanged(
    TextInputManager* text_input_manager,
    RenderWidgetHostViewBase* updated_view) {
  if (!GetTextInputManager())
    return;

  // We obtain the TextSelection from focused RWH which is obtained from the
  // frame tree. BrowserPlugin-based guests' RWH is not part of the frame tree
  // and the focused RWH will be that of the embedder which is incorrect. In
  // this case we should use TextSelection for |this| since RWHV for guest
  // forwards text selection information to its platform view.
  RenderWidgetHostViewBase* focused_view =
      is_guest_view_hack_
          ? this
          : GetFocusedWidget() ? GetFocusedWidget()->GetView() : nullptr;

  if (!focused_view)
    return;

  // IMF relies on the |OnCaretBoundsChanged| for the surrounding text changed
  // events to IME. Explicitly call |OnCaretBoundsChanged| here so that IMF can
  // know about the surrounding text changes when the caret bounds are not
  // changed. e.g. When the rendered text is wider than the input field,
  // deleting the last character won't change the caret bounds but will change
  // the surrounding text.
  if (GetInputMethod())
    GetInputMethod()->OnCaretBoundsChanged(this);

#if defined(USE_X11) || (defined(USE_OZONE) && !defined(OS_CHROMEOS))
  const TextInputManager::TextSelection* selection =
      GetTextInputManager()->GetTextSelection(focused_view);
  if (selection->selected_text().length()) {
    // Set the ClipboardBuffer::kSelection to the ui::Clipboard.
    ui::ScopedClipboardWriter clipboard_writer(ui::ClipboardBuffer::kSelection);
    clipboard_writer.WriteText(selection->selected_text());
  }
#endif  // defined(USE_X11) || (defined(USE_OZONE) && !defined(OS_CHROMEOS))
}

void RenderWidgetHostViewAura::SetPopupChild(
    RenderWidgetHostViewAura* popup_child_host_view) {
  popup_child_host_view_ = popup_child_host_view;
  event_handler_->SetPopupChild(
      popup_child_host_view,
      popup_child_host_view ? popup_child_host_view->event_handler() : nullptr);
}

void RenderWidgetHostViewAura::UpdateNeedsBeginFramesInternal() {
  if (!delegated_frame_host_)
    return;
  delegated_frame_host_->SetNeedsBeginFrames(needs_begin_frames_);
}

void RenderWidgetHostViewAura::ScrollFocusedEditableNodeIntoRect(
    const gfx::Rect& node_rect) {
  RenderFrameHostImpl* rfh = GetFocusedFrame();
  if (!rfh)
    return;

  auto* input_handler = rfh->GetFrameInputHandler();
  if (!input_handler)
    return;
  input_handler->ScrollFocusedEditableNodeIntoRect(node_rect);
}

void RenderWidgetHostViewAura::OnSynchronizedDisplayPropertiesChanged() {
  SynchronizeVisualProperties(cc::DeadlinePolicy::UseDefaultDeadline(),
                              base::nullopt);
}

viz::ScopedSurfaceIdAllocator
RenderWidgetHostViewAura::DidUpdateVisualProperties(
    const cc::RenderFrameMetadata& metadata) {
  base::OnceCallback<void()> allocation_task = base::BindOnce(
      &RenderWidgetHostViewAura::OnDidUpdateVisualPropertiesComplete,
      weak_ptr_factory_.GetWeakPtr(), metadata);
  return window_->GetSurfaceIdAllocator(std::move(allocation_task));
}

void RenderWidgetHostViewAura::DidNavigate() {
  if (!IsShowing()) {
    // Navigating while hidden should not allocate a new LocalSurfaceID. Once
    // sizes are ready, or we begin to Show, we can then allocate the new
    // LocalSurfaceId.
    window_->InvalidateLocalSurfaceId();
  } else {
    if (is_first_navigation_) {
      // The first navigation does not need a new LocalSurfaceID. The renderer
      // can use the ID that was already provided.
      SynchronizeVisualProperties(cc::DeadlinePolicy::UseExistingDeadline(),
                                  window_->GetLocalSurfaceIdAllocation());
    } else {
      SynchronizeVisualProperties(cc::DeadlinePolicy::UseExistingDeadline(),
                                  base::nullopt);
    }
  }
  if (delegated_frame_host_)
    delegated_frame_host_->DidNavigate();
  is_first_navigation_ = false;
}

// static
viz::FrameSinkId
RenderWidgetHostViewAura::AllocateFrameSinkIdForGuestViewHack() {
  return ImageTransportFactory::GetInstance()
      ->GetContextFactoryPrivate()
      ->AllocateFrameSinkId();
}

MouseWheelPhaseHandler* RenderWidgetHostViewAura::GetMouseWheelPhaseHandler() {
  return &event_handler_->mouse_wheel_phase_handler();
}

void RenderWidgetHostViewAura::TakeFallbackContentFrom(
    RenderWidgetHostView* view) {
  DCHECK(!static_cast<RenderWidgetHostViewBase*>(view)
              ->IsRenderWidgetHostViewChildFrame());
  DCHECK(!static_cast<RenderWidgetHostViewBase*>(view)
              ->IsRenderWidgetHostViewGuest());
  RenderWidgetHostViewAura* view_aura =
      static_cast<RenderWidgetHostViewAura*>(view);
  base::Optional<SkColor> color = view_aura->GetBackgroundColor();
  if (color)
    SetBackgroundColor(*color);

  if (delegated_frame_host_ && view_aura->delegated_frame_host_) {
    delegated_frame_host_->TakeFallbackContentFrom(
        view_aura->delegated_frame_host_.get());
  }
  host()->GetContentRenderingTimeoutFrom(view_aura->host());
}

bool RenderWidgetHostViewAura::CanSynchronizeVisualProperties() {
  return !needs_to_update_display_metrics_;
}

std::vector<std::unique_ptr<ui::TouchEvent>>
RenderWidgetHostViewAura::ExtractAndCancelActiveTouches() {
  aura::Env* env = aura::Env::GetInstance();
  std::vector<std::unique_ptr<ui::TouchEvent>> touches =
      env->gesture_recognizer()->ExtractTouches(window());
  CancelActiveTouches();
  return touches;
}

void RenderWidgetHostViewAura::TransferTouches(
    const std::vector<std::unique_ptr<ui::TouchEvent>>& touches) {
  aura::Env* env = aura::Env::GetInstance();
  env->gesture_recognizer()->TransferTouches(window(), touches);
}

void RenderWidgetHostViewAura::InvalidateLocalSurfaceIdOnEviction() {
  window_->InvalidateLocalSurfaceId();
}

void RenderWidgetHostViewAura::ProcessDisplayMetricsChanged() {
  needs_to_update_display_metrics_ = false;
  UpdateScreenInfo(window_);
  current_cursor_.SetDisplayInfo(
      display::Screen::GetScreen()->GetDisplayNearestWindow(window_));
  UpdateCursorIfOverSelf();
}

void RenderWidgetHostViewAura::CancelActiveTouches() {
  aura::Env* env = aura::Env::GetInstance();
  env->gesture_recognizer()->CancelActiveTouches(window());
}

}  // namespace content
