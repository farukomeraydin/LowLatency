#pragma once

#include <thread>
#include <utility>

#include "Assert.h"

namespace Common {

    // Pins the calling thread to one logical processor (CPU core), so the OS always runs it there.
    // coreId counts from 0. Returns false if coreId is invalid or the OS refuses.
    [[nodiscard]] bool setThreadCore(int coreId) noexcept;

    // Starts a new thread that first pins itself to coreId, then runs function().
    // coreId < 0 means "do not pin". If pinning fails, the program stops (fatal).
    // The returned std::jthread joins (waits for the thread to finish) when it is destroyed.
    template <typename Function>
    [[nodiscard]] std::jthread createAndStartThread(int coreId, Function&& function) {
        return std::jthread{[coreId, function = std::forward<Function>(function)]() mutable {
            if (coreId >= 0 && !setThreadCore(coreId)) {
                fatal("createAndStartThread: could not pin thread to the requested core");
            }
            function();
        }};
    }

}
