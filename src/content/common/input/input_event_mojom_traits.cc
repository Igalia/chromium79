// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/input/input_event_mojom_traits.h"

#include "base/i18n/char_iterator.h"
#include "content/common/input_messages.h"
#include "mojo/public/cpp/base/time_mojom_traits.h"
#include "third_party/blink/public/platform/web_keyboard_event.h"
#include "third_party/blink/public/platform/web_mouse_wheel_event.h"
#include "ui/latency/mojom/latency_info_mojom_traits.h"

namespace mojo {
namespace {

void CopyString(blink::WebUChar* dst, const base::string16& text) {
  base::i18n::UTF16CharIterator iter(&text);
  size_t pos = 0;
  while (!iter.end() && pos < blink::WebKeyboardEvent::kTextLengthCap - 1) {
    dst[pos++] = iter.get();
    iter.Advance();
  }
  dst[pos] = '\0';
}

content::mojom::PointerDataPtr PointerDataFromPointerProperties(
    const blink::WebPointerProperties& pointer,
    content::mojom::MouseDataPtr mouse_data) {
  return content::mojom::PointerData::New(
      pointer.id, pointer.force, pointer.tilt_x, pointer.tilt_y,
      pointer.tangential_pressure, pointer.twist, pointer.button,
      pointer.pointer_type, pointer.movement_x, pointer.movement_y,
      pointer.is_raw_movement_event, pointer.PositionInWidget(),
      pointer.PositionInScreen(), std::move(mouse_data));
}

void PointerPropertiesFromPointerData(
    const content::mojom::PointerDataPtr& pointer_data,
    blink::WebPointerProperties* pointer_properties) {
  pointer_properties->id = pointer_data->pointer_id;
  pointer_properties->force = pointer_data->force;
  pointer_properties->tilt_x = pointer_data->tilt_x;
  pointer_properties->tilt_y = pointer_data->tilt_y;
  pointer_properties->tangential_pressure = pointer_data->tangential_pressure;
  pointer_properties->twist = pointer_data->twist;
  pointer_properties->button = pointer_data->button;
  pointer_properties->pointer_type = pointer_data->pointer_type;
  pointer_properties->movement_x = pointer_data->movement_x;
  pointer_properties->movement_y = pointer_data->movement_y;
  pointer_properties->is_raw_movement_event =
      pointer_data->is_raw_movement_event;
}

void TouchPointPropertiesFromPointerData(
    const content::mojom::TouchPointPtr& mojo_touch_point,
    blink::WebTouchPoint* touch_point) {
  PointerPropertiesFromPointerData(mojo_touch_point->pointer_data, touch_point);
  touch_point->state = mojo_touch_point->state;
  touch_point->radius_x = mojo_touch_point->radius_x;
  touch_point->radius_y = mojo_touch_point->radius_y;
  touch_point->rotation_angle = mojo_touch_point->rotation_angle;
  touch_point->SetPositionInWidget(
      mojo_touch_point->pointer_data->widget_position.x(),
      mojo_touch_point->pointer_data->widget_position.y());
  touch_point->SetPositionInScreen(
      mojo_touch_point->pointer_data->screen_position.x(),
      mojo_touch_point->pointer_data->screen_position.y());
}

// TODO(dtapuska): Remove once SetPositionInXXX moves to WebPointerProperties.
void MouseEventPropertiesFromPointerData(
    const content::mojom::PointerDataPtr& pointer_data,
    blink::WebMouseEvent* mouse_event) {
  PointerPropertiesFromPointerData(pointer_data, mouse_event);
  mouse_event->SetPositionInWidget(pointer_data->widget_position.x(),
                                   pointer_data->widget_position.y());
  mouse_event->SetPositionInScreen(pointer_data->screen_position.x(),
                                   pointer_data->screen_position.y());
}

}  // namespace

bool StructTraits<content::mojom::EventDataView, InputEventUniquePtr>::Read(
    content::mojom::EventDataView event,
    InputEventUniquePtr* out) {
  DCHECK(!out->get());

  out->reset(new content::InputEvent());

  blink::WebInputEvent::Type type;
  if (!event.ReadType(&type))
    return false;

  base::TimeTicks timestamp;
  if (!event.ReadTimestamp(&timestamp))
    return false;

  if (blink::WebInputEvent::IsKeyboardEventType(type)) {
    content::mojom::KeyDataPtr key_data;
    if (!event.ReadKeyData<content::mojom::KeyDataPtr>(&key_data))
      return false;

    (*out)->web_event.reset(
        new blink::WebKeyboardEvent(type, event.modifiers(), timestamp));

    blink::WebKeyboardEvent* key_event =
        static_cast<blink::WebKeyboardEvent*>((*out)->web_event.get());
    key_event->windows_key_code = key_data->windows_key_code;
    key_event->native_key_code = key_data->native_key_code;
    key_event->dom_code = key_data->dom_code;
    key_event->dom_key = key_data->dom_key;
    key_event->is_system_key = key_data->is_system_key;
    key_event->is_browser_shortcut = key_data->is_browser_shortcut;
    CopyString(key_event->text, key_data->text);
    CopyString(key_event->unmodified_text, key_data->unmodified_text);
  } else if (blink::WebInputEvent::IsGestureEventType(type)) {
    content::mojom::GestureDataPtr gesture_data;
    if (!event.ReadGestureData<content::mojom::GestureDataPtr>(&gesture_data))
      return false;
    (*out)->web_event.reset(new blink::WebGestureEvent(
        type, event.modifiers(), timestamp, gesture_data->source_device));

    blink::WebGestureEvent* gesture_event =
        static_cast<blink::WebGestureEvent*>((*out)->web_event.get());
    gesture_event->SetPositionInWidget(gesture_data->widget_position);
    gesture_event->SetPositionInScreen(gesture_data->screen_position);
    gesture_event->is_source_touch_event_set_non_blocking =
        gesture_data->is_source_touch_event_set_non_blocking;
    gesture_event->primary_pointer_type = gesture_data->primary_pointer_type;
    gesture_event->SetSourceDevice(gesture_data->source_device);
    gesture_event->unique_touch_event_id = gesture_data->unique_touch_event_id;
    gesture_event->resending_plugin_id = gesture_data->resending_plugin_id;

    if (gesture_data->contact_size) {
      switch (type) {
        default:
          break;
        case blink::WebInputEvent::Type::kGestureTapDown:
          gesture_event->data.tap_down.width =
              gesture_data->contact_size->width();
          gesture_event->data.tap_down.height =
              gesture_data->contact_size->height();
          break;
        case blink::WebInputEvent::Type::kGestureShowPress:
          gesture_event->data.show_press.width =
              gesture_data->contact_size->width();
          gesture_event->data.show_press.height =
              gesture_data->contact_size->height();
          break;
        case blink::WebInputEvent::Type::kGestureTap:
        case blink::WebInputEvent::Type::kGestureTapUnconfirmed:
        case blink::WebInputEvent::Type::kGestureDoubleTap:
          gesture_event->data.tap.width = gesture_data->contact_size->width();
          gesture_event->data.tap.height = gesture_data->contact_size->height();
          break;
        case blink::WebInputEvent::Type::kGestureLongPress:
        case blink::WebInputEvent::Type::kGestureLongTap:
          gesture_event->data.long_press.width =
              gesture_data->contact_size->width();
          gesture_event->data.long_press.height =
              gesture_data->contact_size->height();
          break;
        case blink::WebInputEvent::Type::kGestureTwoFingerTap:
          gesture_event->data.two_finger_tap.first_finger_width =
              gesture_data->contact_size->width();
          gesture_event->data.two_finger_tap.first_finger_height =
              gesture_data->contact_size->height();
          break;
      }
    }

    if (gesture_data->scroll_data) {
      switch (type) {
        default:
          break;
        case blink::WebInputEvent::Type::kGestureScrollBegin:
          gesture_event->data.scroll_begin.delta_x_hint =
              gesture_data->scroll_data->delta_x;
          gesture_event->data.scroll_begin.delta_y_hint =
              gesture_data->scroll_data->delta_y;
          gesture_event->data.scroll_begin.delta_hint_units =
              gesture_data->scroll_data->delta_units;
          gesture_event->data.scroll_begin.target_viewport =
              gesture_data->scroll_data->target_viewport;
          gesture_event->data.scroll_begin.inertial_phase =
              gesture_data->scroll_data->inertial_phase;
          gesture_event->data.scroll_begin.synthetic =
              gesture_data->scroll_data->synthetic;
          gesture_event->data.scroll_begin.pointer_count =
              gesture_data->scroll_data->pointer_count;
          break;
        case blink::WebInputEvent::Type::kGestureScrollEnd:
          gesture_event->data.scroll_end.delta_units =
              gesture_data->scroll_data->delta_units;
          gesture_event->data.scroll_end.inertial_phase =
              gesture_data->scroll_data->inertial_phase;
          gesture_event->data.scroll_end.synthetic =
              gesture_data->scroll_data->synthetic;
          break;
        case blink::WebInputEvent::Type::kGestureScrollUpdate:
          gesture_event->data.scroll_update.delta_x =
              gesture_data->scroll_data->delta_x;
          gesture_event->data.scroll_update.delta_y =
              gesture_data->scroll_data->delta_y;
          gesture_event->data.scroll_update.delta_units =
              gesture_data->scroll_data->delta_units;
          gesture_event->data.scroll_update.inertial_phase =
              gesture_data->scroll_data->inertial_phase;
          if (gesture_data->scroll_data->update_details) {
            gesture_event->data.scroll_update.velocity_x =
                gesture_data->scroll_data->update_details->velocity_x;
            gesture_event->data.scroll_update.velocity_y =
                gesture_data->scroll_data->update_details->velocity_y;
          }
          break;
      }
    }

    if (gesture_data->pinch_begin_data &&
        type == blink::WebInputEvent::Type::kGesturePinchBegin) {
      gesture_event->data.pinch_begin.needs_wheel_event =
          gesture_data->pinch_begin_data->needs_wheel_event;
    }

    if (gesture_data->pinch_update_data &&
        type == blink::WebInputEvent::Type::kGesturePinchUpdate) {
      gesture_event->data.pinch_update.zoom_disabled =
          gesture_data->pinch_update_data->zoom_disabled;
      gesture_event->data.pinch_update.scale =
          gesture_data->pinch_update_data->scale;
      gesture_event->data.pinch_update.needs_wheel_event =
          gesture_data->pinch_update_data->needs_wheel_event;
    }

    if (gesture_data->pinch_end_data &&
        type == blink::WebInputEvent::Type::kGesturePinchEnd) {
      gesture_event->data.pinch_end.needs_wheel_event =
          gesture_data->pinch_end_data->needs_wheel_event;
    }

    if (gesture_data->tap_data) {
      switch (type) {
        default:
          break;
        case blink::WebInputEvent::Type::kGestureTap:
        case blink::WebInputEvent::Type::kGestureTapUnconfirmed:
        case blink::WebInputEvent::Type::kGestureDoubleTap:
          gesture_event->data.tap.tap_count = gesture_data->tap_data->tap_count;
          gesture_event->data.tap.needs_wheel_event =
              gesture_data->tap_data->needs_wheel_event;
          break;
      }
    }

    if (gesture_data->fling_data) {
      switch (type) {
        default:
          break;
        case blink::WebInputEvent::Type::kGestureFlingStart:
          gesture_event->data.fling_start.velocity_x =
              gesture_data->fling_data->velocity_x;
          gesture_event->data.fling_start.velocity_y =
              gesture_data->fling_data->velocity_y;
          gesture_event->data.fling_start.target_viewport =
              gesture_data->fling_data->target_viewport;
          break;
        case blink::WebInputEvent::Type::kGestureFlingCancel:
          gesture_event->data.fling_cancel.target_viewport =
              gesture_data->fling_data->target_viewport;
          gesture_event->data.fling_cancel.prevent_boosting =
              gesture_data->fling_data->prevent_boosting;
          break;
      }
    }

  } else if (blink::WebInputEvent::IsTouchEventType(type)) {
    content::mojom::TouchDataPtr touch_data;
    if (!event.ReadTouchData<content::mojom::TouchDataPtr>(&touch_data))
      return false;

    (*out)->web_event.reset(
        new blink::WebTouchEvent(type, event.modifiers(), timestamp));

    blink::WebTouchEvent* touch_event =
        static_cast<blink::WebTouchEvent*>((*out)->web_event.get());
    std::vector<content::mojom::TouchPointPtr> touches;
    unsigned i;
    for (i = 0; i < touch_data->touches.size() &&
                i < blink::WebTouchEvent::kTouchesLengthCap;
         ++i) {
      blink::WebTouchPoint& touch_point = touch_event->touches[i];
      TouchPointPropertiesFromPointerData(touch_data->touches[i], &touch_point);
    }

    touch_event->touches_length = i;
    touch_event->dispatch_type = touch_data->cancelable;
    touch_event->moved_beyond_slop_region =
        touch_data->moved_beyond_slop_region;
    touch_event->hovering = touch_data->hovering;
    touch_event->touch_start_or_first_touch_move =
        touch_data->touch_start_or_first_move;
    touch_event->unique_touch_event_id = touch_data->unique_touch_event_id;
  } else if (blink::WebInputEvent::IsMouseEventType(type) ||
             type == blink::WebInputEvent::Type::kMouseWheel) {
    content::mojom::PointerDataPtr pointer_data;
    if (!event.ReadPointerData<content::mojom::PointerDataPtr>(&pointer_data))
      return false;

    if (blink::WebInputEvent::IsMouseEventType(type)) {
      (*out)->web_event.reset(
          new blink::WebMouseEvent(type, event.modifiers(), timestamp));
    } else {
      (*out)->web_event.reset(
          new blink::WebMouseWheelEvent(type, event.modifiers(), timestamp));
    }

    blink::WebMouseEvent* mouse_event =
        static_cast<blink::WebMouseEvent*>((*out)->web_event.get());

    MouseEventPropertiesFromPointerData(pointer_data, mouse_event);
    if (pointer_data->mouse_data) {
      mouse_event->click_count = pointer_data->mouse_data->click_count;

      if (type == blink::WebInputEvent::Type::kMouseWheel &&
          pointer_data->mouse_data->wheel_data) {
        blink::WebMouseWheelEvent* wheel_event =
            static_cast<blink::WebMouseWheelEvent*>(mouse_event);
        content::mojom::WheelDataPtr& wheel_data =
            pointer_data->mouse_data->wheel_data;
        wheel_event->delta_x = wheel_data->delta_x;
        wheel_event->delta_y = wheel_data->delta_y;
        wheel_event->wheel_ticks_x = wheel_data->wheel_ticks_x;
        wheel_event->wheel_ticks_y = wheel_data->wheel_ticks_y;
        wheel_event->acceleration_ratio_x = wheel_data->acceleration_ratio_x;
        wheel_event->acceleration_ratio_y = wheel_data->acceleration_ratio_y;
        wheel_event->resending_plugin_id = wheel_data->resending_plugin_id;
        wheel_event->phase =
            static_cast<blink::WebMouseWheelEvent::Phase>(wheel_data->phase);
        wheel_event->momentum_phase =
            static_cast<blink::WebMouseWheelEvent::Phase>(
                wheel_data->momentum_phase);
        wheel_event->dispatch_type = wheel_data->cancelable;
        wheel_event->event_action =
            static_cast<blink::WebMouseWheelEvent::EventAction>(
                wheel_data->event_action);
        wheel_event->delta_units =
            static_cast<ui::input_types::ScrollGranularity>(
                wheel_data->delta_units);
      }
    }

  } else {
    return false;
  }

  return event.ReadLatency(&((*out)->latency_info));
}

// static
content::mojom::KeyDataPtr
StructTraits<content::mojom::EventDataView, InputEventUniquePtr>::key_data(
    const InputEventUniquePtr& event) {
  if (!event->web_event ||
      !blink::WebInputEvent::IsKeyboardEventType(event->web_event->GetType()))
    return nullptr;
  const blink::WebKeyboardEvent* key_event =
      static_cast<const blink::WebKeyboardEvent*>(event->web_event.get());
  return content::mojom::KeyData::New(
      key_event->dom_key, key_event->dom_code, key_event->windows_key_code,
      key_event->native_key_code, key_event->is_system_key,
      key_event->is_browser_shortcut, key_event->text,
      key_event->unmodified_text);
}

// static
content::mojom::PointerDataPtr
StructTraits<content::mojom::EventDataView, InputEventUniquePtr>::pointer_data(
    const InputEventUniquePtr& event) {
  if (!event->web_event)
    return nullptr;
  bool is_wheel_event =
      event->web_event->GetType() == blink::WebInputEvent::Type::kMouseWheel;
  if (!blink::WebInputEvent::IsMouseEventType(event->web_event->GetType()) &&
      !is_wheel_event) {
    return nullptr;
  }
  const blink::WebMouseEvent* mouse_event =
      static_cast<const blink::WebMouseEvent*>(event->web_event.get());

  content::mojom::WheelDataPtr wheel_data;
  if (is_wheel_event) {
    const blink::WebMouseWheelEvent* wheel_event =
        static_cast<const blink::WebMouseWheelEvent*>(mouse_event);
    wheel_data = content::mojom::WheelData::New(
        wheel_event->delta_x, wheel_event->delta_y, wheel_event->wheel_ticks_x,
        wheel_event->wheel_ticks_y, wheel_event->acceleration_ratio_x,
        wheel_event->acceleration_ratio_y, wheel_event->resending_plugin_id,
        wheel_event->phase, wheel_event->momentum_phase,
        wheel_event->dispatch_type,
        static_cast<uint8_t>(wheel_event->event_action),
        static_cast<uint8_t>(wheel_event->delta_units));
  }

  return PointerDataFromPointerProperties(
      *mouse_event, content::mojom::MouseData::New(mouse_event->click_count,
                                                   std::move(wheel_data)));
}

// static
content::mojom::GestureDataPtr
StructTraits<content::mojom::EventDataView, InputEventUniquePtr>::gesture_data(
    const InputEventUniquePtr& event) {
  if (!event->web_event ||
      !blink::WebInputEvent::IsGestureEventType(event->web_event->GetType()))
    return nullptr;
  const blink::WebGestureEvent* gesture_event =
      static_cast<const blink::WebGestureEvent*>(event->web_event.get());
  auto gesture_data = content::mojom::GestureData::New();
  gesture_data->screen_position = gesture_event->PositionInScreen();
  gesture_data->widget_position = gesture_event->PositionInWidget();
  gesture_data->source_device = gesture_event->SourceDevice();
  gesture_data->is_source_touch_event_set_non_blocking =
      gesture_event->is_source_touch_event_set_non_blocking;
  gesture_data->primary_pointer_type = gesture_event->primary_pointer_type;
  gesture_data->unique_touch_event_id = gesture_event->unique_touch_event_id;
  gesture_data->resending_plugin_id = gesture_event->resending_plugin_id;
  switch (gesture_event->GetType()) {
    default:
      break;
    case blink::WebInputEvent::Type::kGestureTapDown:
      gesture_data->contact_size =
          gfx::Size(gesture_event->data.tap_down.width,
                    gesture_event->data.tap_down.height);
      break;
    case blink::WebInputEvent::Type::kGestureShowPress:
      gesture_data->contact_size =
          gfx::Size(gesture_event->data.show_press.width,
                    gesture_event->data.show_press.height);
      break;
    case blink::WebInputEvent::Type::kGestureTap:
    case blink::WebInputEvent::Type::kGestureTapUnconfirmed:
    case blink::WebInputEvent::Type::kGestureDoubleTap:
      gesture_data->contact_size = gfx::Size(gesture_event->data.tap.width,
                                             gesture_event->data.tap.height);
      gesture_data->tap_data = content::mojom::TapData::New(
          gesture_event->data.tap.tap_count,
          gesture_event->data.tap.needs_wheel_event);
      break;
    case blink::WebInputEvent::Type::kGestureLongPress:
    case blink::WebInputEvent::Type::kGestureLongTap:
      gesture_data->contact_size =
          gfx::Size(gesture_event->data.long_press.width,
                    gesture_event->data.long_press.height);
      break;

    case blink::WebInputEvent::Type::kGestureTwoFingerTap:
      gesture_data->contact_size =
          gfx::Size(gesture_event->data.two_finger_tap.first_finger_width,
                    gesture_event->data.two_finger_tap.first_finger_height);
      break;
    case blink::WebInputEvent::Type::kGestureScrollBegin:
      gesture_data->scroll_data = content::mojom::ScrollData::New(
          gesture_event->data.scroll_begin.delta_x_hint,
          gesture_event->data.scroll_begin.delta_y_hint,
          gesture_event->data.scroll_begin.delta_hint_units,
          gesture_event->data.scroll_begin.target_viewport,
          gesture_event->data.scroll_begin.inertial_phase,
          gesture_event->data.scroll_begin.synthetic,
          gesture_event->data.scroll_begin.pointer_count, nullptr);
      break;
    case blink::WebInputEvent::Type::kGestureScrollEnd:
      gesture_data->scroll_data = content::mojom::ScrollData::New(
          0, 0, gesture_event->data.scroll_end.delta_units, false,
          gesture_event->data.scroll_end.inertial_phase,
          gesture_event->data.scroll_end.synthetic, 0, nullptr);
      break;
    case blink::WebInputEvent::Type::kGestureScrollUpdate:
      gesture_data->scroll_data = content::mojom::ScrollData::New(
          gesture_event->data.scroll_update.delta_x,
          gesture_event->data.scroll_update.delta_y,
          gesture_event->data.scroll_update.delta_units, false,
          gesture_event->data.scroll_update.inertial_phase, false, 0,
          content::mojom::ScrollUpdate::New(
              gesture_event->data.scroll_update.velocity_x,
              gesture_event->data.scroll_update.velocity_y));
      break;
    case blink::WebInputEvent::Type::kGestureFlingStart:
      gesture_data->fling_data = content::mojom::FlingData::New(
          gesture_event->data.fling_start.velocity_x,
          gesture_event->data.fling_start.velocity_y,
          gesture_event->data.fling_start.target_viewport, false);
      break;
    case blink::WebInputEvent::Type::kGestureFlingCancel:
      gesture_data->fling_data = content::mojom::FlingData::New(
          0, 0, gesture_event->data.fling_cancel.target_viewport,
          gesture_event->data.fling_cancel.prevent_boosting);
      break;
    case blink::WebInputEvent::Type::kGesturePinchBegin:
      gesture_data->pinch_begin_data = content::mojom::PinchBeginData::New(
          gesture_event->data.pinch_begin.needs_wheel_event);
      break;
    case blink::WebInputEvent::Type::kGesturePinchUpdate:
      gesture_data->pinch_update_data = content::mojom::PinchUpdateData::New(
          gesture_event->data.pinch_update.scale,
          gesture_event->data.pinch_update.zoom_disabled,
          gesture_event->data.pinch_update.needs_wheel_event);
      break;
    case blink::WebInputEvent::Type::kGesturePinchEnd:
      gesture_data->pinch_end_data = content::mojom::PinchEndData::New(
          gesture_event->data.pinch_end.needs_wheel_event);
      break;
  }
  return gesture_data;
}

// static
content::mojom::TouchDataPtr
StructTraits<content::mojom::EventDataView, InputEventUniquePtr>::touch_data(
    const InputEventUniquePtr& event) {
  if (!event->web_event ||
      !blink::WebInputEvent::IsTouchEventType(event->web_event->GetType()))
    return nullptr;

  const blink::WebTouchEvent* touch_event =
      static_cast<const blink::WebTouchEvent*>(event->web_event.get());
  auto touch_data = content::mojom::TouchData::New(
      touch_event->dispatch_type, touch_event->moved_beyond_slop_region,
      touch_event->touch_start_or_first_touch_move, touch_event->hovering,
      touch_event->unique_touch_event_id,
      std::vector<content::mojom::TouchPointPtr>());
  for (unsigned i = 0; i < touch_event->touches_length; ++i) {
    content::mojom::PointerDataPtr pointer_data =
        PointerDataFromPointerProperties(touch_event->touches[i], nullptr);
    touch_data->touches.emplace_back(content::mojom::TouchPoint::New(
        touch_event->touches[i].state, touch_event->touches[i].radius_x,
        touch_event->touches[i].radius_y,
        touch_event->touches[i].rotation_angle, std::move(pointer_data)));
  }
  return touch_data;
}

}  // namespace mojo
