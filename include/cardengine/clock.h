#pragma once

// Portable wall-clock helpers for the tournament director and action
// timers. The engine never sleeps, never spawns threads, and never decides
// policy: it only stamps "since when" (seconds since an unspecified epoch,
// from std::chrono::steady_clock) and compares stamps callers hand back.
// Hosts enforce (disconnect timers, blind clocks, action clocks); tests
// inject exact stamps, so clock behavior stays deterministic with no sleeps.
//
// Units are seconds everywhere. Durations are integers: fractional-second
// poker clocks are a GUI animation concern, not engine state.
#include <chrono>
#include <cstdint>

namespace cardengine {

// Seconds since an unspecified steady epoch. Monotonic: never goes
// backwards (system-clock adjustments do not touch it).
inline std::int64_t now_seconds() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::seconds>(
               clock::now().time_since_epoch())
        .count();
}

// True when `now - since >= limit` (with limit <= 0 meaning "already due").
// Negative elapsed (a stamp from a newer boot) never counts as expired.
inline bool expired(std::int64_t now, std::int64_t since, std::int64_t limit) {
    if (limit <= 0) return true;
    if (now < since) return false;
    return now - since >= limit;
}

}  // namespace cardengine
