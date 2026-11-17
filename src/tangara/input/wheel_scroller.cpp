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
      last_move_us_(0),
      last_pos_(0),
      accum_x16_(0),
      pending_reverse_(0),
      speed_(0),
      travel_(0),
      first_move_(false),
      jump_allowed_(false),
      jumping_(false),
      jump_accum_(0),
      jumps_(0),
      jump_ended_(false) {}

auto WheelScroller::setJumpAllowed(bool allowed) -> void {
  jump_allowed_ = allowed;
}

auto WheelScroller::endJump() -> void {
  jumping_ = false;
  jump_ended_ = true;
  jump_accum_ = 0;
  // Carry on scrolling item by item from here, starting afresh.
  accum_x16_ = 0;
  pending_reverse_ = 0;
  travel_ = 0;
  first_move_ = false;
}

auto WheelScroller::setStep(int units) -> void {
  step_x16_ = std::clamp(units, kMinStep, kMaxStep) * kSubUnits;
}

auto WheelScroller::setAcceleration(bool enabled) -> void {
  accel_ = enabled;
}

auto WheelScroller::gainX16() const -> int {
  if (!accel_ || travel_ < kAccelMinTravel || speed_ <= kAccelStartSpeed) {
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
  jumps_ = 0;
  jump_ended_ = false;
  if (jumping_ && !jump_allowed_) {
    endJump();
  }

  if (!touched) {
    touching_ = false;
    if (jumping_) {
      endJump();
    }
    return 0;
  }

  if (!touching_) {
    // Finger just landed. Wherever it landed becomes the reference point;
    // landing never scrolls.
    touching_ = true;
    touch_start_us_ = now_us;
    last_sample_us_ = now_us;
    last_move_us_ = now_us;
    last_pos_ = position;
    accum_x16_ = 0;
    pending_reverse_ = 0;
    speed_ = 0;
    travel_ = 0;
    first_move_ = true;
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

  // If the UI was busy (e.g. loading list items) then samples arrive late,
  // and a fast-moving finger can legitimately travel a long way between them.
  // Only treat big jumps as glitches when the samples were close together.
  int max_jump = dt_us > kSlowSampleUs ? kMaxJumpSlowSample : kMaxJump;
  if (std::abs(movement) > max_jump) {
    // Implausible jump. Re-anchor at the new position and carry on.
    speed_ = 0;
    travel_ = 0;
    pending_reverse_ = 0;
    return 0;
  }

  if (movement == 0) {
    // The touch controller doesn't report a new position on every sample, so
    // a single still sample doesn't mean the finger has stopped.
    if (now_us - last_move_us_ > kStillUs) {
      speed_ = 0;
      travel_ = 0;
      if (jumping_) {
        endJump();
      }
    }
    return 0;
  }

  // Measure speed between samples that actually moved.
  uint64_t move_dt_us = std::min<uint64_t>(now_us - last_move_us_, kStillUs);
  last_move_us_ = now_us;
  if (move_dt_us > 0) {
    int instant = static_cast<int>(std::min<uint64_t>(
        std::abs(movement) * 1'000'000ull / move_dt_us, 4096));
    speed_ = (speed_ + instant) / 2;
  }

  if (jumping_) {
    if (speed_ < kJumpExitSpeed) {
      // Slowed down; go back to scrolling item by item.
      endJump();
      return 0;
    }
    jump_accum_ += movement;
    jumps_ = jump_accum_ / kJumpStep;
    jump_accum_ -= jumps_ * kJumpStep;
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
    travel_ = 0;
  } else {
    // Still going the same way; any absorbed wobble cancels out.
    movement += pending_reverse_;
    pending_reverse_ = 0;
  }

  if (first_move_ && movement != 0) {
    // The first item of a stroke is cheaper than the rest. Without this,
    // there would be a range of small movements too big to be a tap but too
    // small to scroll, which makes nudging by one item feel unresponsive.
    first_move_ = false;
    int head_start = step_x16_ - kFirstStep * kSubUnits;
    if (head_start > 0) {
      accum_x16_ += sign(movement) * head_start;
    }
  }

  travel_ += std::abs(movement);

  if (jump_allowed_ && travel_ >= kJumpMinTravel &&
      speed_ >= kJumpStartSpeed) {
    // Spinning fast through a long list. Jump straight away, so that it's
    // obvious the wheel has changed mode.
    jumping_ = true;
    jump_accum_ = 0;
    accum_x16_ = 0;
    jumps_ = sign(movement);
    return 0;
  }

  accum_x16_ += movement * gainX16();

  int ticks = accum_x16_ / step_x16_;
  accum_x16_ -= ticks * step_x16_;
  return std::clamp(ticks, -127, 127);
}

}  // namespace input
