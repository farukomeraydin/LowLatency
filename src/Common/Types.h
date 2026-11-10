#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "Assert.h"

namespace Common {

    using OrderId  = std::uint64_t;   // unique id of an order
    using TickerId = std::uint32_t;   // instrument id (AAPL -> 0, MSFT -> 1, ...)
    using ClientId = std::uint32_t;   // id of a trader connected to the exchange
    using Price    = std::int64_t;    // price in ticks (150.28 -> 15028)
    using Qty      = std::uint32_t;   // quantity
    using Priority = std::uint32_t;   // place of an order in the FIFO queue at its price (1 = first)

    // Size of one CPU cache line in bytes (64 on x86-64). Data written by different threads should
    // live on different cache lines, otherwise the cores keep stealing the line from each other.
    inline constexpr std::size_t CACHE_LINE_SIZE = 64;

    // "No value" markers: the largest value of each type is reserved to mean "invalid".
    inline constexpr OrderId  OrderId_INVALID  = std::numeric_limits<OrderId>::max();
    inline constexpr TickerId TickerId_INVALID = std::numeric_limits<TickerId>::max();
    inline constexpr ClientId ClientId_INVALID = std::numeric_limits<ClientId>::max();
    inline constexpr Price    Price_INVALID    = std::numeric_limits<Price>::max();
    inline constexpr Qty      Qty_INVALID      = std::numeric_limits<Qty>::max();
    inline constexpr Priority Priority_INVALID = std::numeric_limits<Priority>::max();

    // Side of an order. BUY = +1 and SELL = -1 so the side can be used as a sign in arithmetic.
    enum class Side : std::int8_t {
        INVALID = 0,
        BUY     = 1,
        SELL    = -1
    };

    // BUY -> +1, SELL -> -1. Returned as a signed 64-bit value so that "sideToValue(side) * qty"
    // is computed in signed arithmetic (Qty is unsigned).
    [[nodiscard]] constexpr std::int64_t sideToValue(Side side) noexcept {
        return static_cast<std::int64_t>(side);
    }

    // Number of valid sides (BUY, SELL). Used as the size of arrays indexed by side.
    inline constexpr std::size_t SIDE_COUNT = 2;

    // BUY -> 0, SELL -> 1. Used as an array index. side must not be INVALID.
    [[nodiscard]] constexpr std::size_t sideToIndex(Side side) noexcept {
        assertTrue(side != Side::INVALID, "sideToIndex: side must not be INVALID");
        return static_cast<std::size_t>((1 - sideToValue(side)) / 2);
    }

    // Text form of a side, for logging and printing. Returns a view of a string literal,
    // so there is no allocation and the view is always valid.
    [[nodiscard]] constexpr std::string_view sideToString(Side side) noexcept {
        switch (side) {
            case Side::BUY:     return "BUY";
            case Side::SELL:    return "SELL";
            case Side::INVALID: return "INVALID";
        }
        return "UNKNOWN";   // a value that is none of the enumerators, e.g. static_cast<Side>(5)
    }

    // Text form of an id, price or quantity, for logging and printing:
    // "INVALID" for the sentinel value (the type's max, see the *_INVALID constants), otherwise the number.
    template <std::integral T>
    [[nodiscard]] std::string valueToString(T value) {
        if (value == std::numeric_limits<T>::max()) {
            return "INVALID";
        }
        return std::to_string(value);
    }

}
