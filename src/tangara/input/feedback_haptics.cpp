/*
 * Copyright 2024 jacqueline <me@jacqueline.id.au>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "input/feedback_haptics.hpp"

#include <cstdint>

#include "core/lv_group.h"
#include "esp_log.h"
#include "lvgl/lvgl.h"

#include "drivers/haptics.hpp"
#include "esp_timer.h"

namespace input {

// The motor can't play distinct clicks closer together than this. Firing them
// faster (for example while spinning the wheel quickly) just makes it buzz.
static constexpr int64_t kMinFocusHapticGapUs = 60'000;

using Effect = drivers::Haptics::Effect;

Haptics::Haptics(drivers::Haptics& haptics_,
                 std::shared_ptr<system_fsm::ServiceLocator> services)
    : haptics_(haptics_),
      services_(services),
      last_selection_(nullptr),
      last_focus_haptic_us_(0) {}

auto Haptics::feedback(lv_group_t* group, uint8_t event_type) -> void {
  lv_obj_t* obj = lv_group_get_focused(group);

  auto haptics_mode = services_->nvs().HapticsMode();
  if (haptics_mode == drivers::NvsStorage::HapticsModes::kDisabled ) {
    return;
  }

  switch (event_type) {
    case LV_EVENT_FOCUSED:
      if (obj == last_selection_) {
        return;
      }
      last_selection_ = obj;
      if (int64_t now = esp_timer_get_time();
          now - last_focus_haptic_us_ < kMinFocusHapticGapUs) {
        return;
      } else {
        last_focus_haptic_us_ = now;
      }
      if (haptics_mode == drivers::NvsStorage::HapticsModes::kMinimal) {
        haptics_.PlayWaveformEffect(Effect::kStrongClick_30Pct);
      } else {
        haptics_.PlayWaveformEffect(Effect::kMediumClick1_100Pct);
      }
      break;
    case LV_EVENT_CLICKED:
      if (haptics_mode == drivers::NvsStorage::HapticsModes::kMinimal) {
        haptics_.PlayWaveformEffect(Effect::kSharpClick_30Pct);
      } else {
        haptics_.PlayWaveformEffect(Effect::kSharpClick_100Pct);
      }
      break;
    default:
      break;
  }
}

auto Haptics::feedback(lv_group_t* group, InputEvent event) -> void {
  lv_obj_t* obj = lv_group_get_focused(group);

  auto haptics_mode = services_->nvs().HapticsMode();
  if (haptics_mode == drivers::NvsStorage::HapticsModes::kDisabled) {
    return;
  }

  switch (event) {
    case InputEvent::kOnPress:
      if (haptics_mode == drivers::NvsStorage::HapticsModes::kMinimal) {
        haptics_.PlayWaveformEffect(Effect::kSoftBump_30Pct);
      } else {
        haptics_.PlayWaveformEffect(Effect::kSharpTick2_80Pct);
      }
      break;
    case InputEvent::kOnLongPress:
      if (haptics_mode == drivers::NvsStorage::HapticsModes::kMinimal) {
        haptics_.PlayWaveformEffect(Effect::kShortDoubleClickStrong4_30Pct);
      } else {
        haptics_.PlayWaveformEffect(Effect::kShortDoubleSharpTick3_60Pct);
      }
      break;
    default:
      break;
  }
}

}  // namespace input
