#include <cstdint>
#include <thread>

#include "Common/LFQueue.h"
#include "Test.h"

using Common::LFQueue;

TEST(queueIsFifo) {
    LFQueue<int> queue(8);
    for (int i = 0; i < 5; ++i) {
        CHECK(queue.tryPush(i));
    }
    int value = -1;
    for (int i = 0; i < 5; ++i) {
        CHECK(queue.tryPop(value));
        CHECK(value == i);
    }
    CHECK(!queue.tryPop(value));
}

TEST(queueRejectsPushWhenFull) {
    LFQueue<int> queue(4);
    for (int i = 0; i < 4; ++i) {
        CHECK(queue.tryPush(i));
    }
    CHECK(!queue.tryPush(99));

    int value = -1;
    CHECK(queue.tryPop(value));
    CHECK(value == 0);
    CHECK(queue.tryPush(4));
}

TEST(queueWrapsAround) {
    LFQueue<std::uint64_t> queue(4);
    std::uint64_t value = 0;
    for (std::uint64_t i = 0; i < 1000; ++i) {
        CHECK(queue.tryPush(i));
        CHECK(queue.tryPop(value));
        CHECK(value == i);
    }
}

TEST(queuePassesEveryMessageBetweenThreads) {
    constexpr std::uint64_t COUNT = 2'000'000;
    LFQueue<std::uint64_t> queue(64);

    std::thread producer([&queue] {
        for (std::uint64_t i = 0; i < COUNT; ++i) {
            while (!queue.tryPush(i)) {
            }
        }
    });

    std::uint64_t expected = 0;
    std::uint64_t outOfOrder = 0;
    std::uint64_t value = 0;
    while (expected < COUNT) {
        if (queue.tryPop(value)) {
            outOfOrder += (value != expected) ? 1 : 0;
            ++expected;
        }
    }
    producer.join();

    CHECK(outOfOrder == 0);
    CHECK(!queue.tryPop(value));
}
