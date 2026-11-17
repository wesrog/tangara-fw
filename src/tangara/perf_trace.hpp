/*
 * SPDX-License-Identifier: GPL-3.0-only
 */

// Lightweight timing logs for finding where time goes when starting and
// skipping tracks. Everything is logged under the "perf" tag.

#pragma once

#include <atomic>
#include <cstdint>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace perf {

inline std::atomic<int64_t> sRequestUs{0};

inline auto now() -> int64_t {
  return esp_timer_get_time();
}

// Marks the moment the user asked for something (play, skip).
inline auto markRequest(const char* what) -> void {
  sRequestUs = now();
  ESP_LOGI("perf", "== %s", what);
}

inline auto sinceRequestMs() -> int32_t {
  return static_cast<int32_t>((now() - sRequestUs.load()) / 1000);
}

// Logs a milestone relative to the last request.
inline auto milestone(const char* what) -> void {
  ESP_LOGI("perf", "%s at +%ld ms [%s]", what, sinceRequestMs(),
           pcTaskGetName(NULL));
}

// Logs how long a scope took.
class Scope {
 public:
  explicit Scope(const char* label, int32_t min_ms = 0)
      : label_(label), min_ms_(min_ms), start_(now()) {}
  ~Scope() {
    int32_t ms = static_cast<int32_t>((now() - start_) / 1000);
    if (ms >= min_ms_) {
      ESP_LOGI("perf", "%s took %ld ms (+%ld ms) [%s]", label_, ms,
               sinceRequestMs(), pcTaskGetName(NULL));
    }
  }

 private:
  const char* label_;
  int32_t min_ms_;
  int64_t start_;
};

// Running stats for a hot call. Logs a summary every `every` calls.
class Stats {
 public:
  Stats(const char* label, uint32_t every) : label_(label), every_(every) {}

  auto add(int64_t us) -> void {
    count_++;
    total_us_ += us;
    if (us > max_us_) {
      max_us_ = us;
    }
    if (count_ >= every_) {
      ESP_LOGI("perf", "%s: %lu calls, avg %lld us, max %lld us [%s]", label_,
               count_, total_us_ / count_, max_us_, pcTaskGetName(NULL));
      count_ = 0;
      total_us_ = 0;
      max_us_ = 0;
    }
  }

 private:
  const char* label_;
  uint32_t every_;
  uint32_t count_ = 0;
  int64_t total_us_ = 0;
  int64_t max_us_ = 0;
};

}  // namespace perf
