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
  const int64_t SCROLL_TIMEOUT_US = 250000;  // 250ms

  // How far (in wheel units, 256 per revolution) the finger may drift while
  // tapping one of the wheel's edges before the touch stops counting as a tap.
  static constexpr int kTapSlop = 12;

  drivers::NvsStorage& nvs_;
  drivers::TouchWheel& wheel_;

  audio::TrackQueue& queue_;

  TriggerHooks centre_;
  TriggerHooks up_;
  TriggerHooks right_;
  TriggerHooks down_;
  TriggerHooks left_;

  bool locked_;
  bool is_scrolling_;
  int64_t last_scroll_us_;
  WheelScroller scroller_;
  int64_t last_wheel_touch_time_;
  bool was_touched_;
  uint8_t touch_down_pos_;
  bool moved_since_touch_;
};

}  // namespace input
