/*
 * Copyright 2024 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <sys/_stdint.h>
#include <cstdint>

#include "indev/lv_indev.h"

#include "drivers/haptics.hpp"
#include "drivers/nvs.hpp"
#include "drivers/touchwheel.hpp"
#include "input/input_device.hpp"
#include "input/input_hook.hpp"
#include "input/input_trigger.hpp"
#include "input/wheel_scroller.hpp"
#include "lua/property.hpp"

namespace input {

class TouchWheel : public IInputDevice {
 public:
  TouchWheel(drivers::NvsStorage&, drivers::TouchWheel&, audio::TrackQueue&);

  auto read(lv_indev_data_t* data,
            std::vector<InputEvent>& events) -> void override;

  auto name() -> std::string override;
  auto triggers() -> std::vector<std::reference_wrapper<TriggerHooks>> override;

  auto onLock() -> void override;
  auto onUnlock() -> void override;

 private:
  // The centre button ignores presses this soon after the wheel scrolled, so
  // that a thumb sweeping past it mid-scroll doesn't select anything.
  static constexpr int64_t kCentreAfterScrollUs = 150'000;

  // How far (in wheel units, 256 per revolution) the finger may drift while
  // tapping one of the wheel's edges before the touch stops counting as a tap.
  static constexpr int kTapSlop = 6;
  // Taps must land this close (in wheel units) to the middle of an edge. This
  // leaves dead zones between the edges, where a tap never triggers anything.
  static constexpr int kTapZone = 28;
  // Touches longer than this aren't taps. It matches the long-press delay, so
  // that every press is either a tap or a hold, with no gap in between where
  // nothing happens. Measured to when the finger last touched, not to the
  // end of kReleaseGraceUs.
  static constexpr int64_t kMaxTapUs = 400'000;
  // Touches that start this soon after scrolling aren't taps, since they're
  // probably the next stroke of the scroll.
  static constexpr int64_t kTapAfterScrollUs = 200'000;
  // Holds are much harder to trigger by accident than taps (the finger has to
  // stay put for the whole long-press delay), so they're more forgiving. A
  // hold may land anywhere in an edge's quarter of the wheel, start straight
  // after a scroll, and drift a little, as long as it doesn't scroll.
  static constexpr int kHoldZone = 32;
  static constexpr int kHoldSlop = 8;
  // Touch dropouts shorter than this are ignored.
  static constexpr int64_t kReleaseGraceUs = 60'000;

  drivers::NvsStorage& nvs_;
  drivers::TouchWheel& wheel_;

  audio::TrackQueue& queue_;

  TriggerHooks centre_;
  TriggerHooks up_;
  TriggerHooks right_;
  TriggerHooks down_;
  TriggerHooks left_;
  // Jump mode, for long lists (see WheelScroller). These have no actions of
  // their own; lists set their click hooks from Lua.
  TriggerHooks jump_next_;
  TriggerHooks jump_prev_;
  TriggerHooks jump_end_;
  // Sent when the finger lifts after a touch that jumped, so that the list can
  // stop showing the letter.
  TriggerHooks jump_release_;
  bool jumped_this_touch_;

  bool locked_;
  bool is_scrolling_;
  int64_t last_scroll_us_;
  WheelScroller scroller_;
  bool was_touched_;
  uint8_t touch_down_pos_;
  int64_t touch_down_us_;
  int64_t last_raw_touch_us_;
  // Which edge (index into kEdgeAngles) the current touch landed on, or -1.
  int touch_edge_;
  // Whether the current touch can still become a tap, or a hold.
  bool tap_ok_;
  bool hold_ok_;
};

}  // namespace input
