/*
 * Copyright 2026 wes
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "input/wheel_scroller.hpp"

#include <cmath>
#include <cstdint>

#include "catch2/catch.hpp"

namespace input {

// The LVGL input read period on Tangara (CONFIG_LV_DEF_REFR_PERIOD).
static constexpr uint64_t kSampleUs = 33'000;

struct FakeFinger {
  WheelScroller& scroller;
  uint64_t now_us = 1'000'000;
  double pos = 100;

  auto land() -> int { return sample(); }

  auto sample() -> int {
    uint8_t raw =
        static_cast<uint8_t>(static_cast<int>(std::lround(pos)) & 0xFF);
    int t = scroller.update(true, raw, now_us);
    now_us += kSampleUs;
    return t;
  }

  // Holds still for a while, letting the touch settle.
  auto rest(uint64_t us) -> int {
    int ticks = 0;
    for (uint64_t t = 0; t < us; t += kSampleUs) {
      ticks += sample();
    }
    return ticks;
  }

  // Moves by `units` (negative is anticlockwise, i.e. positive ticks) at a
  // constant speed in units per second.
  auto move(double units, double units_per_sec) -> int {
    double per_sample = units_per_sec * kSampleUs / 1e6;
    int samples = std::max(
        1, static_cast<int>(std::lround(std::abs(units) / per_sample)));
    double step = units / samples;
    int ticks = 0;
    for (int i = 0; i < samples; i++) {
      pos += step;
      ticks += sample();
    }
    return ticks;
  }

  auto lift() -> void {
    scroller.update(false, 0, now_us);
    now_us += kSampleUs;
  }
};

TEST_CASE("wheel scroller", "[unit]") {
  WheelScroller scroller;
  scroller.setStep(20);
  FakeFinger finger{scroller};

  SECTION("landing and resting never scroll") {
    REQUIRE(finger.land() == 0);
    REQUIRE(finger.rest(2'000'000) == 0);
  }

  SECTION("a jittery resting finger never scrolls") {
    finger.land();
    finger.rest(100'000);
    int ticks = 0;
    for (int i = 0; i < 100; i++) {
      finger.pos = 100 + ((i % 2) ? 4 : -4);
      ticks += finger.sample();
    }
    REQUIRE(ticks == 0);
  }

  SECTION("slow scrolling costs the same rotation for every item") {
    scroller.setAcceleration(false);
    finger.land();
    finger.rest(100'000);
    // Half a revolution per second.
    REQUIRE(finger.move(-200, 128) == 10);
    REQUIRE(finger.move(200, 128) == -10);
  }

  SECTION("fast spins don't drop items when acceleration is off") {
    scroller.setAcceleration(false);
    finger.land();
    finger.rest(100'000);
    // Five revolutions at 3 revolutions per second, ~25 units per sample.
    REQUIRE(finger.move(-1280, 768) == 64);
  }

  SECTION("direction is reversed") {
    scroller.setAcceleration(false);
    finger.land();
    finger.rest(100'000);
    // 8 for the first item, then 20 each; 2 units of progress left over.
    REQUIRE(finger.move(-50, 128) == 3);
    // Turning around costs exactly one step from the turnaround point.
    REQUIRE(finger.move(19, 128) == 0);
    REQUIRE(finger.move(1, 128) == -1);
  }

  SECTION("small backwards wobbles don't lose progress") {
    scroller.setAcceleration(false);
    finger.land();
    finger.rest(100'000);
    REQUIRE(finger.move(-8, 128) == 1);
    REQUIRE(finger.move(-15, 128) == 0);
    REQUIRE(finger.move(2, 128) == 0);
    REQUIRE(finger.move(-7, 128) == 1);  // net 20 units
  }

  SECTION("impossible jumps are ignored") {
    finger.land();
    finger.rest(100'000);
    finger.pos += 100;
    REQUIRE(finger.sample() == 0);
    REQUIRE(finger.rest(500'000) == 0);
  }

  SECTION("movement during a busy UI isn't dropped") {
    scroller.setAcceleration(false);
    finger.land();
    finger.rest(100'000);
    // One 150ms gap between samples, during which the finger moved 100 units.
    finger.now_us += 117'000;
    finger.pos -= 100;
    REQUIRE(finger.sample() == 5);
  }

  SECTION("the controller skipping samples doesn't stop acceleration") {
    finger.land();
    finger.rest(100'000);
    // 2 revolutions per second, but the controller only reports a new
    // position on every other sample.
    int ticks = 0;
    for (int i = 0; i < 60; i++) {
      if (i % 2 == 0) {
        finger.pos -= 2 * 512.0 * kSampleUs / 1e6;
      }
      ticks += finger.sample();
    }
    // ~1000 units of rotation would be ~50 ticks without acceleration.
    REQUIRE(ticks > 50 * 2);
  }

  SECTION("the first moments after landing are ignored") {
    finger.land();
    finger.pos -= 25;
    REQUIRE(finger.sample() == 0);
  }

  SECTION("acceleration") {
    finger.land();
    finger.rest(100'000);

    // Slow scrolling is never accelerated.
    REQUIRE(finger.move(-200, 192) == 10);

    // Fast spins go further than the same rotation done slowly.
    int fast = finger.move(-1280, 1024);
    REQUIRE(fast > 64 * 3);
    REQUIRE(fast <= 64 * WheelScroller::kMaxGain);
  }

  SECTION("the first item of a touch is cheaper") {
    finger.land();
    finger.rest(100'000);
    REQUIRE(finger.move(-WheelScroller::kFirstStep + 1, 128) == 0);
    REQUIRE(finger.move(-1, 128) == 1);
    // After that, a full step per item.
    REQUIRE(finger.move(-19, 128) == 0);
    REQUIRE(finger.move(-1, 128) == 1);
  }

  SECTION("quick short nudges are never accelerated") {
    finger.land();
    finger.rest(100'000);
    // A fast flick: 8 units for the first item, then 20 per item.
    REQUIRE(finger.move(-48, 1024) == 3);
  }

  SECTION("lifting resets everything") {
    scroller.setAcceleration(false);
    finger.land();
    finger.rest(100'000);
    REQUIRE(finger.move(-7, 128) == 0);
    finger.lift();
    finger.land();
    finger.rest(100'000);
    REQUIRE(finger.move(-7, 128) == 0);
  }
}

TEST_CASE("wheel scroller default step", "[unit]") {
  WheelScroller scroller;
  FakeFinger finger{scroller};
  scroller.setAcceleration(false);
  finger.land();
  finger.rest(100'000);
  REQUIRE(finger.move(-WheelScroller::kDefaultStep, 128) == 1);
}

}  // namespace input
