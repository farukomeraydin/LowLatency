#pragma once

#include <chrono>
#include <cstdint>
#include <format>
#include <string>

namespace Common {

    using Nanos = std::int64_t;   // a time or a duration, in nanoseconds

    // Nanoseconds since an arbitrary starting point, from a steady (never adjusted) clock.
    // Only the difference between two calls is meaningful: it measures elapsed time.
    [[nodiscard]] inline Nanos getCurrentNanos() noexcept {
        const auto sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch).count();
    }

    // Nanoseconds since 1970-01-01 00:00:00 UTC (Unix time), from the wall clock.
    // Use it to record WHEN something happened (logs, message timestamps), not to measure durations:
    // the OS may adjust this clock at any moment.
    [[nodiscard]] inline Nanos getWallClockNanos() noexcept {
        const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch).count();
    }

    // Text form of a getWallClockNanos() value, in UTC: "YYYY-MM-DD HH:MM:SS.nnnnnnnnn".
    // Slow (builds a string): for logs and printing, never on the hot path.
    [[nodiscard]] inline std::string wallClockToString(Nanos wallClockNanos) {
        const std::chrono::sys_time<std::chrono::nanoseconds> timePoint{std::chrono::nanoseconds{wallClockNanos}};
        return std::format("{:%F %T}", timePoint);
    }

}
