#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "../Common/Types.h"

namespace Exchange {

    // What changed in an order book. Sent to EVERYONE (market data), so it never says whose order it is.
    // One byte; INVALID = 0, so a zero-initialized update is never mistaken for a real one.
    enum class MarketUpdateType : std::uint8_t {
        INVALID = 0,
        ADD     = 1,   // a new order rests in the book
        MODIFY  = 2,   // a resting order's quantity changed (it was partly filled)
        CANCEL  = 3,   // a resting order left the book (canceled, or fully filled)
        TRADE   = 4    // a trade happened (for information: the book itself changes through MODIFY/CANCEL)
    };

    // Text form, for logging and printing. A view of a string literal: no allocation, always valid.
    [[nodiscard]] constexpr std::string_view marketUpdateTypeToString(MarketUpdateType type) noexcept {
        switch (type) {
            case MarketUpdateType::ADD:     return "ADD";
            case MarketUpdateType::MODIFY:  return "MODIFY";
            case MarketUpdateType::CANCEL:  return "CANCEL";
            case MarketUpdateType::TRADE:   return "TRADE";
            case MarketUpdateType::INVALID: return "INVALID";
        }
        return "UNKNOWN";   // a value that is none of the enumerators, e.g. a corrupted message
    }

    // One change of an order book, published to all clients (multicast, copied byte for byte with memcpy).
    // Same rules as ClientRequest: largest fields first, explicit zeroed padding, everything starts invalid.
    // There is deliberately NO client id: market data is anonymous.
    struct MarketUpdate {
        Common::OrderId marketOrderId = Common::OrderId_INVALID;    // which resting order (INVALID for TRADE)
        Common::Price price = Common::Price_INVALID;                 // the order's price (TRADE: the trade price)
        Common::TickerId tickerId = Common::TickerId_INVALID;        // which instrument
        Common::Qty qty = Common::Qty_INVALID;                       // ADD/MODIFY: qty now in the book; TRADE: qty traded
        Common::Priority priority = Common::Priority_INVALID;        // the order's place in the FIFO queue at its price
        MarketUpdateType type = MarketUpdateType::INVALID;
        Common::Side side = Common::Side::INVALID;                   // TRADE: the aggressor's side
        std::uint8_t reserved[2] = {};                               // explicit padding, always 0
    };

    static_assert(sizeof(MarketUpdate) == 32);
    static_assert(std::has_unique_object_representations_v<MarketUpdate>);
    static_assert(std::is_trivially_copyable_v<MarketUpdate>);

}
