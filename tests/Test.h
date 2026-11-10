#pragma once

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace Test {

    struct Case {
        const char* name;
        void (*run)();
    };

    inline std::vector<Case>& cases() {
        static std::vector<Case> all;
        return all;
    }

    inline int& failedChecks() {
        static int count = 0;
        return count;
    }

    struct Registrar {
        Registrar(const char* name, void (*run)()) { cases().push_back({name, run}); }
    };

    inline void fail(const char* expression, const char* file, int line) {
        std::cout << "    " << file << "(" << line << "): CHECK(" << expression << ") failed\n";
        ++failedChecks();
    }

    // Polls `condition` until it is true or `timeout` passes. For tests that wait on sockets or threads.
    template <typename Condition>
    bool waitFor(Condition condition, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!condition()) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

}

#define TEST(name)                                                 \
    static void name();                                            \
    static const Test::Registrar name##Registrar{#name, &name};    \
    static void name()

#define CHECK(expression)                                          \
    do {                                                           \
        if (!(expression)) {                                       \
            Test::fail(#expression, __FILE__, __LINE__);           \
        }                                                          \
    } while (false)
