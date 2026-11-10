#include <cstring>
#include <iostream>

#include "Test.h"

// Usage: LowLatencyTests [filter]   runs the tests whose name contains `filter` (all if omitted).
int main(int argc, char* argv[]) {
    const char* filter = (argc > 1) ? argv[1] : "";

    int run = 0;
    int failed = 0;
    for (const Test::Case& test : Test::cases()) {
        if (std::strstr(test.name, filter) == nullptr) {
            continue;
        }
        const int before = Test::failedChecks();
        test.run();
        const bool ok = Test::failedChecks() == before;
        std::cout << (ok ? "  ok    " : "  FAIL  ") << test.name << "\n";
        ++run;
        failed += ok ? 0 : 1;
    }

    std::cout << "\n" << run - failed << "/" << run << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
