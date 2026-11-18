/*
 * Copyright 2024 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "input/input_touch_wheel.hpp"

#include <cstdint>
#include <variant>

#include "core/lv_group.h"
#include "core/lv_obj.h"
#include "indev/lv_indev.h"

#include "drivers/haptics.hpp"
#include "drivers/nvs.hpp"
#include "drivers/touchwheel.hpp"
#include "events/event_queue.hpp"
#include "input/input_device.hpp"
#include "input/input_hook_actions.hpp"
#include "input/input_trigger.hpp"
#include "lua/property.hpp"
#include "ui/ui_events.hpp"

#include "esp_timer.h"

namespace input {

TouchWheel::TouchWheel(drivers::NvsStorage& nvs,
                       drivers::TouchWheel& wheel,
                       audio::TrackQueue& queue)
    : nvs_(nvs),
      wheel_(wheel),
      queue_(queue),
      centre_("centre", actions::select(), {}, actions::longPress(), {}),
      // Laid out like a classic click wheel. There are deliberately no
      // double-click actions: those fire as soon as the finger lands, which
      // happens constantly when scrolling with repeated thumb strokes.
      up_("up", actions::goBack(), {}, {}, {}),
      right_("right", actions::nextTrack(queue), {}, {}, {}),
      down_("down", actions::togglePlayPause(), {}, {}, {}),
      left_("left", actions::prevTrack(queue), {}, actions::goBack(), {}),
      jump_next_("jump_next", {}),
      jump_prev_("jump_prev", {}),
      jump_end_("jump_end", {}),
      jump_release_("jump_release", {}),
      jumped_this_touch_(false),
      locked_(false),
      is_scrolling_(false),
      last_scroll_us_(0),
      scroller_(),
      was_touched_(false),
      touch_down_pos_(0),
      touch_down_us_(0),
      last_raw_touch_us_(0),
      touch_edge_(-1),
      tap_ok_(false),
      hold_ok_(false) {}

// Wheel positions of the middle of each edge, in the same order as the
// triggers in edgeTriggers(): up, left, down, right.
static constexpr int16_t kEdgeAngles[] = {0, 64, 128, 192};

// Returns the edge that `pos` is within `zone` units of, or -1 if none.
static auto edgeAt(uint8_t pos, int zone) -> int {
  for (int i = 0; i < 4; i++) {
    if (drivers::TouchWheel::isAngleWithin(pos, kEdgeAngles[i], zone)) {
      return i;
    }
  }
  return -1;
}

auto TouchWheel::read(lv_indev_data_t* data,
                      std::vector<InputEvent>& events) -> void {
  if (locked_) {
    return;
  }

  wheel_.Update();
  auto wheel_data = wheel_.GetTouchWheelData();
  const int64_t now_us = esp_timer_get_time();

  // A light touch can briefly drop out for a reading or two. Ride through
  // short dropouts instead of treating them as the finger lifting, which would
  // throw away scroll progress and make light scrolling feel uneven.
  if (wheel_data.is_wheel_touched) {
    last_raw_touch_us_ = now_us;
  } else if (was_touched_ && now_us - last_raw_touch_us_ < kReleaseGraceUs) {
    wheel_data.is_wheel_touched = true;
  }
  // Long lists mark their rows with USER_1, to say that spinning fast should
  // jump through them (e.g. by letter) rather than scroll row by row.
  lv_group_t* group = lv_group_get_default();
  lv_obj_t* focused = group ? lv_group_get_focused(group) : nullptr;
  scroller_.setJumpAllowed(focused &&
                           lv_obj_has_flag(focused, LV_OBJ_FLAG_USER_1));

  int ticks = scroller_.update(wheel_data.is_wheel_touched,
                               wheel_data.wheel_position, now_us);

  // Hand jumps to the list. This happens before this read's ticks (if any)
  // are applied, so the list is already where the jumps left it.
  if (scroller_.jumpEnded()) {
    jump_end_.hooks()[0].get().invoke(data);
  }
  for (int i = 0; i < scroller_.jumps(); i++) {
    jump_next_.hooks()[0].get().invoke(data);
  }
  for (int i = 0; i > scroller_.jumps(); i--) {
    jump_prev_.hooks()[0].get().invoke(data);
  }
  if (scroller_.jumps() != 0) {
    // One click per jump, for haptic feedback.
    events.push_back(InputEvent::kOnPress);
    jumped_this_touch_ = true;
  }

  const bool touched = wheel_data.is_wheel_touched;
  const bool landed = touched && !was_touched_;
  const bool released = !touched && was_touched_;

  if (released && jumped_this_touch_) {
    jumped_this_touch_ = false;
    jump_release_.hooks()[0].get().invoke(data);
  }

  // Work out what this touch could still be: a tap on an edge, a hold on an
  // edge, or neither. Taps are strict, so that scrolling never sets them off.
  // Holds are forgiving, since a finger only stays put for that long on
  // purpose.
  if (landed) {
    touch_down_pos_ = wheel_data.wheel_position;
    touch_down_us_ = now_us;
    touch_edge_ = edgeAt(wheel_data.wheel_position, kHoldZone);
    hold_ok_ = touch_edge_ >= 0;
    // A touch that starts soon after a scroll is the next stroke of that
    // scroll, not a tap.
    tap_ok_ = hold_ok_ && now_us - last_scroll_us_ >= kTapAfterScrollUs &&
              drivers::TouchWheel::isAngleWithin(
                  wheel_data.wheel_position, kEdgeAngles[touch_edge_],
                  kTapZone);
  } else if (touched) {
    int travel = static_cast<int8_t>(
        static_cast<uint8_t>(wheel_data.wheel_position - touch_down_pos_));
    if (travel < 0) {
      travel = -travel;
    }
    if (travel > kTapSlop) {
      tap_ok_ = false;
    }
    if (travel > kHoldSlop) {
      hold_ok_ = false;
    }
  }
  if ((touched || released) &&
      last_raw_touch_us_ - touch_down_us_ > kMaxTapUs) {
    // Resting on the wheel for a while isn't a tap. (It may be a hold.)
    tap_ok_ = false;
  }
  if (touched && (ticks != 0 || scroller_.jumping())) {
    // Anything that scrolls is neither a tap nor a hold.
    tap_ok_ = false;
    hold_ok_ = false;
  }
  was_touched_ = wheel_data.is_wheel_touched;

  if (!wheel_data.is_wheel_touched) {
    // User has released the wheel.
    is_scrolling_ = false;
    data->enc_diff = 0;
  } else if (ticks != 0 || scroller_.jumping()) {
    // User is touching the wheel, and has just passed the sensitivity
    // threshold for a scroll tick (or is spinning through a long list).
    is_scrolling_ = true;
    data->enc_diff = ticks;
  } else {
    // User is touching the wheel, but hasn't moved.
    data->enc_diff = 0;
  }

  if (is_scrolling_) {
    last_scroll_us_ = now_us;
  }

  // Prevent accidental center button touches while scrolling.
  const bool centre_pressed = wheel_data.is_button_touched &&
                              !wheel_data.is_wheel_touched &&
                              now_us - last_scroll_us_ > kCentreAfterScrollUs;
  auto centre_state = centre_.update(centre_pressed, data);
  if (centre_state == input::Trigger::State::kPress) {
    events.push_back(InputEvent::kOnPress);
  } else if (centre_state == input::Trigger::State::kLongPress) {
    events.push_back(InputEvent::kOnLongPress);
  }
  if (!centre_pressed) {
    // Each press stands alone. Without this, a second press within the
    // double-click delay is reported as a double-click, which the centre has
    // no action for, so the press is lost.
    centre_.cancel();
  }

  // Edge taps and holds. Only the edge the touch landed on is ever pressed,
  // so sliding into or out of an edge's zone doesn't start or end a press.
  TriggerHooks* edges[] = {&up_, &left_, &down_, &right_};
  for (int i = 0; i < 4; i++) {
    TriggerHooks& edge = *edges[i];
    if (i != touch_edge_) {
      edge.cancel();
      continue;
    }
    if ((touched && !tap_ok_ && !hold_ok_) || (released && !tap_ok_)) {
      // This touch can't be a tap any more, so make sure letting go doesn't
      // click. (If a hold already fired, letting go does nothing anyway.)
      edge.cancel();
    }
    const auto state = edge.update(touched && (tap_ok_ || hold_ok_), data);
    // This is for haptic feedback.
    if (state == Trigger::State::kClick) {
      events.push_back(InputEvent::kOnPress);
    } else if (state == Trigger::State::kLongPress) {
      events.push_back(InputEvent::kOnLongPress);
      // A hold only fires once per touch.
      tap_ok_ = false;
    }
    if (!touched) {
      // Each tap stands alone. Without this, a second tap soon after the
      // first is reported as a double-click, and its tap action is swallowed.
      edge.cancel();
    }
  }
  if (!touched) {
    touch_edge_ = -1;
  }
}

auto TouchWheel::name() -> std::string {
  return "wheel";
}

auto TouchWheel::triggers()
    -> std::vector<std::reference_wrapper<TriggerHooks>> {
  return {centre_, up_, right_, down_, left_, jump_next_, jump_prev_,
          jump_end_, jump_release_};
}

auto TouchWheel::onLock() -> void {
  wheel_.LowPowerMode(true);
  locked_ = true;
}

auto TouchWheel::onUnlock() -> void {
  wheel_.LowPowerMode(false);
  wheel_.Recalibrate();
  locked_ = false;
}

}  // namespace input
