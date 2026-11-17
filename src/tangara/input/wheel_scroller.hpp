/*
 * Copyright 2026 wes
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <cstdint>

namespace input {

/*
 * Turns raw touch wheel positions into scroll ticks.
 *
 * This is deliberately free of any hardware or LVGL dependencies so that it
 * can be unit tested. Positions are in the touch controller's units, where
 * 256 units is one full revolution of the wheel.
 *
 * The goal is for the wheel to feel like a classic click wheel:
 *  - Every item costs the same amount of rotation. Movement past a tick is
 *    carried over to the next one rather than thrown away, so fast spins don't
 *    drop items.
 *  - Small wobbles of a resting finger never produce ticks, and reversing
 *    direction costs exactly one step from the point where you turned around.
 *  - Spinning quickly scrolls further per revolution, so long lists can be
 *    crossed without wearing out your thumb. Slow movement is never
 *    accelerated, so precise selection stays precise.
 */
class WheelScroller {
 public:
  // Rotation (in wheel units) per scroll tick when moving slowly. The default
  // is about 22 degrees, or 16 items per revolution.
  static constexpr int kDefaultStep = 16;
  static constexpr int kMinStep = 5;
  // The first tick after the finger lands only needs this much rotation.
  static constexpr int kFirstStep = 8;
  static constexpr int kMaxStep = 35;

  // A single-sample jump larger than this (90 degrees) is not a plausible
  // finger movement. It's either a sensor glitch, or the finger has lifted
  // and landed somewhere else between samples.
  static constexpr int kMaxJump = 64;
  // When samples arrive late because the UI was busy, allow larger jumps (up
  // to just under half a revolution, beyond which direction is ambiguous).
  static constexpr uint64_t kSlowSampleUs = 50'000;
  static constexpr int kMaxJumpSlowSample = 112;

  // How long the finger must be still before we consider it stopped.
  static constexpr uint64_t kStillUs = 100'000;

  // How much the finger may wobble backwards before we treat it as a
  // deliberate change of direction.
  static constexpr int kReverseHysteresis = 3;

  // Position readings are noisy for a moment after the finger lands.
  static constexpr uint64_t kSettleUs = 50'000;

  // Acceleration curve. Below kAccelStartSpeed (units per second) there is
  // no acceleration. Gain then ramps linearly up to kMaxGain at
  // kAccelFullSpeed. For reference, 256 units/s is one revolution per second.
  static constexpr int kAccelStartSpeed = 256;
  static constexpr int kAccelFullSpeed = 640;
  // Kept low: list rows take a while to redraw, so a higher gain just makes
  // the list lurch along in bursts, and buzzes the haptics.
  static constexpr int kMaxGain = 2;
  // Acceleration only kicks in once the finger has travelled this far in one
  // direction without stopping, so that quick short nudges (to move one or
  // two items) are never accelerated past the item you wanted.
  static constexpr int kAccelMinTravel = 96;

  // Jump mode, for long lists. Spinning faster than kJumpStartSpeed, after at
  // least kJumpMinTravel of travel, stops scrolling item by item and instead
  // reports a jump (e.g. to the next letter) every kJumpStep of rotation.
  // Jump mode ends when the spin slows below kJumpExitSpeed, the finger stops,
  // or the finger lifts.
  static constexpr int kJumpStartSpeed = 640;
  static constexpr int kJumpExitSpeed = 192;
  static constexpr int kJumpMinTravel = 128;
  static constexpr int kJumpStep = 40;

  WheelScroller();

  auto setStep(int units) -> void;
  auto setAcceleration(bool enabled) -> void;

  // Feeds in one sample, and returns the number of ticks to scroll by.
  // Positive ticks are anticlockwise, matching the existing wheel behaviour.
  auto update(bool touched, uint8_t position, uint64_t now_us) -> int;

  // Whether fast spins may switch to jump mode. Set this before update().
  auto setJumpAllowed(bool allowed) -> void;
  // Jumps made by the last update(); positive is anticlockwise, like ticks.
  auto jumps() const -> int { return jumps_; }
  // Whether the last update() left jump mode.
  auto jumpEnded() const -> bool { return jump_ended_; }
  auto jumping() const -> bool { return jumping_; }

  // Exposed for tests and debugging.
  auto speed() const -> int { return speed_; }

 private:
  auto gainX16() const -> int;

  int step_x16_;
  bool accel_;

  bool touching_;
  uint64_t touch_start_us_;
  uint64_t last_sample_us_;
  uint64_t last_move_us_;
  uint8_t last_pos_;

  // Accumulated, gain-adjusted movement in 1/16ths of a unit.
  int accum_x16_;
  // Movement against the current direction that hasn't yet passed the
  // reversal hysteresis, in raw units.
  int pending_reverse_;
  // Smoothed rotational speed, in units per second.
  int speed_;
  // Distance travelled in the current direction without stopping, in units.
  int travel_;
  // Whether the next movement is the first of this touch.
  bool first_move_;

  auto endJump() -> void;

  bool jump_allowed_;
  bool jumping_;
  // Rotation since the last jump, in raw units.
  int jump_accum_;
  int jumps_;
  bool jump_ended_;
};

}  // namespace input
