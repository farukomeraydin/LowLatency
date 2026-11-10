#pragma once

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <string_view>

namespace Common {

    // Prints an error message with the place it was called from, then terminates the program.
    // Used when the program reaches a state it must never be in.
    [[noreturn]] inline void fatal(std::string_view message,
                                   std::source_location location = std::source_location::current()) noexcept {
        std::cerr << "FATAL: " << message << "\n"
                  << "  at " << location.file_name() << '(' << location.line() << ")\n"
                  << "  in " << location.function_name() << "\n";
        std::abort();
    }

    // true in Debug builds, false in Release builds (Visual Studio defines NDEBUG in Release).
#ifdef NDEBUG
    inline constexpr bool ASSERTS_ENABLED = false;
#else
    inline constexpr bool ASSERTS_ENABLED = true;
#endif

    // Debug-only check: if condition is false, calls fatal(). Removed from Release builds.
    // The condition must not have side effects, because Release builds do not need its result.
    // constexpr so it can be used inside constexpr functions. If it fails during compile-time
    // evaluation, the call to the non-constexpr fatal() turns the failure into a compile error.
    constexpr void assertTrue(bool condition, std::string_view message,
                           std::source_location location = std::source_location::current()) noexcept {
        if constexpr (ASSERTS_ENABLED) {
            if (!condition) [[unlikely]] {
                fatal(message, location);
            }
        }
    }

}
