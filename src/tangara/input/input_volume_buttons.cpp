/*
 * Copyright 2024 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "input/input_volume_buttons.hpp"

#include "esp_timer.h"

#include "audio/audio_events.hpp"
#include "drivers/gpios.hpp"
#include "events/event_queue.hpp"
#include "input/input_hook_actions.hpp"

namespace input {

// Holding a volume button repeats every kRepeatDelayMs (100ms). The step per
// repeat doubles each second, so a hold goes 2.5, 5, 10, then 20 dB/s on the
// headphone output. Single presses still move by the smallest step.
static constexpr uint64_t kStepDoublingUs = 1000 * 1000;
static constexpr uint_fast8_t kMaxHeldSteps = 8;

auto VolumeButtons::heldSteps(uint64_t held_since_us) -> uint_fast8_t {
  uint64_t held_us = esp_timer_get_time() - held_since_us;
  uint_fast8_t steps = 1;
  for (uint64_t t = kStepDoublingUs; t <= held_us && steps < kMaxHeldSteps;
       t += kStepDoublingUs) {
    steps *= 2;
  }
  return steps;
}

VolumeButtons::VolumeButtons(drivers::IGpios& gpios)
    : gpios_(gpios),
      up_since_us_(0),
      down_since_us_(0),
      up_was_pressed_(false),
      down_was_pressed_(false),
      up_("upper",
          actions::volumeUp(),
          actions::volumeUp(),
          actions::volumeUp(),
          HookCallback{.name = "volume_up",
                       .fn =
                           [this](lv_indev_data_t*) {
                             events::Audio().Dispatch(audio::StepUpVolume{
                                 .steps = heldSteps(up_since_us_)});
                           }}),
      down_("lower",
            actions::volumeDown(),
            actions::volumeDown(),
            actions::volumeDown(),
            HookCallback{.name = "volume_down",
                         .fn =
                             [this](lv_indev_data_t*) {
                               events::Audio().Dispatch(audio::StepDownVolume{
                                   .steps = heldSteps(down_since_us_)});
                             }}),
      locked_() {}

auto VolumeButtons::read(lv_indev_data_t* data, std::vector<InputEvent>& events) -> void {
  bool up = !gpios_.Get(drivers::IGpios::Pin::kKeyUp);
  bool down = !gpios_.Get(drivers::IGpios::Pin::kKeyDown);

  if ((up && down)) {
    up = false;
    down = false;
  }

  uint64_t now_us = esp_timer_get_time();
  if (up && !up_was_pressed_) {
    up_since_us_ = now_us;
  }
  if (down && !down_was_pressed_) {
    down_since_us_ = now_us;
  }
  up_was_pressed_ = up;
  down_was_pressed_ = down;

  up_.update(up, data);
  down_.update(down, data);
}

auto VolumeButtons::name() -> std::string {
  return "buttons";
}

auto VolumeButtons::triggers()
    -> std::vector<std::reference_wrapper<TriggerHooks>> {
  return {up_, down_};
}

auto VolumeButtons::onLock() -> void {
  locked_ = true;
}

auto VolumeButtons::onUnlock() -> void {
  locked_ = false;
}

}  // namespace input
