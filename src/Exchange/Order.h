#pragma once

#include <format>
#include <string>

#include "../Common/Types.h"

namespace Exchange {

    // One order resting in an order book. Allocated from a MemoryPool, never with new.
    // `prev`/`next` link the orders at the same price in arrival order (FIFO), as a circular doubly linked
    // list: the first order's prev is the last order, the last order's next is the first.
    // This is the matching engine's own record, not a network message: its padding is never sent anywhere.
    struct Order {
        Order() noexcept = default;

        Order(Common::TickerId tickerId, Common::ClientId clientId, Common::OrderId clientOrderId,
              Common::OrderId marketOrderId, Common::Side side, Common::Price price, Common::Qty qty,
              Common::Priority priority) noexcept
            : clientOrderId(clientOrderId), marketOrderId(marketOrderId), price(price), clientId(clientId),
              tickerId(tickerId), qty(qty), priority(priority), side(side) {}

        Order* prev = nullptr;   // the order before this one at the same price (circular)
        Order* next = nullptr;   // the order after this one at the same price (circular)
        Common::OrderId clientOrderId = Common::OrderId_INVALID;
        Common::OrderId marketOrderId = Common::OrderId_INVALID;
        Common::Price price = Common::Price_INVALID;
        Common::ClientId clientId = Common::ClientId_INVALID;
        Common::TickerId tickerId = Common::TickerId_INVALID;
        Common::Qty qty = Common::Qty_INVALID;                    // quantity still waiting (the "leaves qty")
        Common::Priority priority = Common::Priority_INVALID;     // ticket number at its price level
        Common::Side side = Common::Side::INVALID;
    };

    // Text form for debugging and tests (allocates: never on the hot path).
    [[nodiscard]] inline std::string orderToString(const Order& order) {
        const auto idOf = [](const Order* other) {
            return other ? Common::valueToString(other->marketOrderId) : std::string{"none"};
        };
        return std::format("[order {} ticker {} client {} client order {} {} {} @ {} priority {} prev {} next {}]",
                           Common::valueToString(order.marketOrderId), Common::valueToString(order.tickerId),
                           Common::valueToString(order.clientId), Common::valueToString(order.clientOrderId),
                           Common::sideToString(order.side), Common::valueToString(order.qty),
                           Common::valueToString(order.price), Common::valueToString(order.priority),
                           idOf(order.prev), idOf(order.next));
    }

}
