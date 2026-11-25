/*
 * TEMPORARY: counters for chasing a mid-track stutter. Not for merge.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace audio {
namespace dbg {

// Readahead (file reader task).
inline std::atomic<uint32_t> sReadCount{0};
inline std::atomic<uint32_t> sReadBytes{0};
inline std::atomic<uint32_t> sReadUsTotal{0};
inline std::atomic<uint32_t> sReadUsMax{0};
// Readahead (decoder side).
inline std::atomic<uint32_t> sCacheMisses{0};
inline std::atomic<uint32_t> sCacheMissUs{0};
inline std::atomic<uint32_t> sBufferedMin{UINT32_MAX};
// Decoder task.
inline std::atomic<uint32_t> sDecodeUs{0};
inline std::atomic<uint32_t> sDecodeUsMax{0};
inline std::atomic<uint32_t> sDecodedSamples{0};
inline std::atomic<bool> sFlacInPsram{false};

inline auto atomicMax(std::atomic<uint32_t>& a, uint32_t v) -> void {
  uint32_t cur = a.load();
  while (v > cur && !a.compare_exchange_weak(cur, v)) {
  }
}
inline auto atomicMin(std::atomic<uint32_t>& a, uint32_t v) -> void {
  uint32_t cur = a.load();
  while (v < cur && !a.compare_exchange_weak(cur, v)) {
  }
}

}  // namespace dbg
}  // namespace audio
