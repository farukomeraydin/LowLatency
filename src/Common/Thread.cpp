#include "Thread.h"

#include <thread>

#define WIN32_LEAN_AND_MEAN   // leave out rarely used parts of windows.h (faster to compile)
#define NOMINMAX              // stop windows.h from defining min/max macros
#include <windows.h>

namespace Common {

    bool setThreadCore(int coreId) noexcept {
        // An affinity mask has one bit per logical processor, so it can describe at most 64 of them.
        const int coreCount = static_cast<int>(std::thread::hardware_concurrency());
        if (coreId < 0 || coreId >= coreCount || coreId >= 64) {
            return false;
        }

        const DWORD_PTR mask = DWORD_PTR{1} << coreId;
        return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
    }

}
