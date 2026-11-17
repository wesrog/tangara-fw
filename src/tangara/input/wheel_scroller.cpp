/*
 * Copyright 2026 wes
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "input/wheel_scroller.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace input {

static constexpr int kSubUnits = 16;

static auto sign(int v) -> int {
  return (v > 0) - (v < 0);
}

// Shortest signed distance between two wheel positions, in [-128, 127].
static auto wrappedDelta(uint8_t from, uint8_t to) -> int {
  return static_cast<int8_t>(static_cast<uint8_t>(to - from));
}

WheelScroller::WheelScroller()
    : step_x16_(kDefaultStep * kSubUnits),
      accel_(true),
      touching_(false),
      touch_start_us_(0),
      last_sample_us_(0),
      last_pos_(0),
      accum_x16_(0),
      pending_reverse_(0),
      speed_(0) {}

auto WheelScroller::setStep(int units) -> void {
  step_x16_ = std::clamp(units, kMinStep, kMaxStep) * kSubUnits;
}

auto WheelScroller::setAcceleration(bool enabled) -> void {
  accel_ = enabled;
}

auto WheelScroller::gainX16() const -> int {
  if (!accel_ || speed_ <= kAccelStartSpeed) {
    return kSubUnits;
  }
  if (speed_ >= kAccelFullSpeed) {
    return kMaxGain * kSubUnits;
  }
  int extra = (speed_ - kAccelStartSpeed) * (kMaxGain - 1) * kSubUnits /
              (kAccelFullSpeed - kAccelStartSpeed);
  return kSubUnits + extra;
}

auto WheelScroller::update(bool touched,
                           uint8_t position,
                           uint64_t now_us) -> int {
  if (!touched) {
    touching_ = false;
    return 0;
  }

  if (!touching_) {
    // Finger just landed. Wherever it landed becomes the reference point;
    // landing never scrolls.
    touching_ = true;
    touch_start_us_ = now_us;
    last_sample_us_ = now_us;
    last_pos_ = position;
    accum_x16_ = 0;
    pending_reverse_ = 0;
    speed_ = 0;
    return 0;
  }

  // Positive movement is anticlockwise (decreasing position), which is the
  // direction the original implementation treated as a positive tick.
  int movement = -wrappedDelta(last_pos_, position);
  uint64_t dt_us = now_us - last_sample_us_;
  last_pos_ = position;
  last_sample_us_ = now_us;

  if (now_us - touch_start_us_ < kSettleUs) {
    // Keep tracking the position while it settles, but don't act on it.
    return 0;
  }

  if (std::abs(movement) > kMaxJump) {
    // Implausible jump. Re-anchor at the new position and carry on.
    speed_ = 0;
    pending_reverse_ = 0;
    return 0;
  }

  if (dt_us > 0) {
    int instant = static_cast<int>(
        std::min<uint64_t>(std::abs(movement) * 1'000'000ull / dt_us, 4096));
    // Exponential moving average; smooths out samples where the controller
    // hadn't reported a new position yet.
    speed_ = (speed_ * 3 + instant) / 4;
  }

  if (movement == 0) {
    return 0;
  }

  int direction = sign(accum_x16_);
  if (direction != 0 && sign(movement) != direction) {
    // Moving against the current direction. Small wobbles are absorbed;
    // only a deliberate reversal changes direction.
    pending_reverse_ += movement;
    if (std::abs(pending_reverse_) < kReverseHysteresis) {
      return 0;
    }
    // A real reversal. Forget partial progress in the old direction, so that
    // the first tick the other way costs exactly one step from here.
    movement = pending_reverse_;
    pending_reverse_ = 0;
    accum_x16_ = 0;
  } else {
    // Still going the same way; any absorbed wobble cancels out.
    movement += pending_reverse_;
    pending_reverse_ = 0;
  }

  accum_x16_ += movement * gainX16();

  int ticks = accum_x16_ / step_x16_;
  accum_x16_ -= ticks * step_x16_;
  return std::clamp(ticks, -127, 127);
}

}  // namespace input
