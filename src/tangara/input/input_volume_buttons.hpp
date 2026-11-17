/*
 * Copyright 2024 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <cstdint>

#include "indev/lv_indev.h"

#include "drivers/gpios.hpp"
#include "drivers/haptics.hpp"
#include "drivers/touchwheel.hpp"
#include "input/input_device.hpp"
#include "input/input_hook.hpp"

namespace input {

class VolumeButtons : public IInputDevice {
 public:
  VolumeButtons(drivers::IGpios&);

  auto read(lv_indev_data_t* data, std::vector<InputEvent>& events) -> void override;

  auto name() -> std::string override;
  auto triggers() -> std::vector<std::reference_wrapper<TriggerHooks>> override;

  auto onLock() -> void override;
  auto onUnlock() -> void override;

 private:
  drivers::IGpios& gpios_;

  // How many volume steps one repeat of a held button moves, based on how
  // long it has been held.
  static auto heldSteps(uint64_t held_since_us) -> uint_fast8_t;

  uint64_t up_since_us_;
  uint64_t down_since_us_;
  bool up_was_pressed_;
  bool down_was_pressed_;

  TriggerHooks up_;
  TriggerHooks down_;

  bool locked_;
};

}  // namespace input
