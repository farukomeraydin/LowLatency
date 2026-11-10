#include "Common/Time.h"
#include "Common/Types.h"
#include "Test.h"

using namespace Common;

static_assert(sideToValue(Side::BUY) == 1);
static_assert(sideToValue(Side::SELL) == -1);
static_assert(sideToIndex(Side::BUY) == 0);
static_assert(sideToIndex(Side::SELL) == 1);
static_assert(sideToString(Side::SELL) == "SELL");

TEST(sideToValueKeepsArithmeticSigned) {
    const Qty qty = 100;
    CHECK(sideToValue(Side::SELL) * qty == -100);
    CHECK(sideToValue(Side::BUY) * qty == 100);
}

TEST(sideToStringHandlesBadValues) {
    CHECK(sideToString(Side::BUY) == "BUY");
    CHECK(sideToString(Side::INVALID) == "INVALID");
    CHECK(sideToString(static_cast<Side>(5)) == "UNKNOWN");
}

TEST(valueToStringPrintsSentinelsAsInvalid) {
    CHECK(valueToString(OrderId_INVALID) == "INVALID");
    CHECK(valueToString(Price_INVALID) == "INVALID");
    CHECK(valueToString(Qty{0}) == "0");
    CHECK(valueToString(Price{-15028}) == "-15028");
    CHECK(valueToString(Priority{3}) == "3");
}

TEST(steadyClockNeverGoesBackwards) {
    Nanos previous = getCurrentNanos();
    for (int i = 0; i < 100'000; ++i) {
        const Nanos now = getCurrentNanos();
        CHECK(now >= previous);
        previous = now;
    }
}

TEST(wallClockToStringFormatsUtc) {
    CHECK(wallClockToString(0) == "1970-01-01 00:00:00.000000000");
    CHECK(wallClockToString(1'000'000'000LL * 86'400 + 5) == "1970-01-02 00:00:00.000000005");
}
