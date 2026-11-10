#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Common/MemoryPool.h"
#include "Test.h"

using Common::MemoryPool;

namespace {

    struct Tracked {
        Tracked(int value, int& liveCount) : value(value), liveCount(liveCount) { ++liveCount; }
        ~Tracked() { --liveCount; }

        int value;
        int& liveCount;
    };

    struct alignas(32) Aligned {
        std::uint8_t data[40];
    };

}

TEST(poolConstructsWithArguments) {
    MemoryPool<std::string> pool(4);
    std::string* text = pool.allocate(5, 'x');
    CHECK(*text == "xxxxx");
    pool.deallocate(text);
}

TEST(poolHandsOutDistinctBlocksUntilFull) {
    MemoryPool<int> pool(16);
    std::set<int*> seen;
    for (int i = 0; i < 16; ++i) {
        seen.insert(pool.allocate(i));
    }
    CHECK(seen.size() == 16);
    for (int* p : seen) {
        pool.deallocate(p);
    }
}

TEST(poolReusesTheLastFreedBlock) {
    MemoryPool<int> pool(8);
    int* a = pool.allocate(1);
    int* b = pool.allocate(2);
    pool.deallocate(a);
    CHECK(pool.allocate(3) == a);
    pool.deallocate(b);
    CHECK(pool.allocate(4) == b);
}

TEST(poolRespectsAlignment) {
    MemoryPool<Aligned> pool(8);
    std::vector<Aligned*> blocks;
    for (int i = 0; i < 8; ++i) {
        blocks.push_back(pool.allocate());
        CHECK(reinterpret_cast<std::uintptr_t>(blocks.back()) % alignof(Aligned) == 0);
    }
    for (Aligned* p : blocks) {
        pool.deallocate(p);
    }
}

TEST(poolRunsDestructors) {
    int live = 0;
    {
        MemoryPool<Tracked> pool(4);
        Tracked* first = pool.allocate(1, live);
        Tracked* second = pool.allocate(2, live);
        Tracked* third = pool.allocate(3, live);
        CHECK(live == 3);
        CHECK(second->value == 2 && third->value == 3);
        pool.deallocate(first);
        CHECK(live == 2);
    }
    CHECK(live == 0);   // the pool destroys what is still allocated
}
