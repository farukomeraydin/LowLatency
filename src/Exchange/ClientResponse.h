#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "../Common/Types.h"

namespace Exchange {

    // What the exchange tells a trading client about one of its orders.
    // One byte; INVALID = 0, so a zero-initialized response is never mistaken for a real one.
    enum class ClientResponseType : std::uint8_t {
        INVALID         = 0,
        ACCEPTED        = 1,   // the NEW order was accepted (it may trade right away, see FILLED)
        CANCELED        = 2,   // the order was removed from the book, as the client asked
        FILLED          = 3,   // the order traded (fully or partly: see leavesQty)
        CANCEL_REJECTED = 4    // the CANCEL could not be done (e.g. the order is already fully filled)
    };

    // Text form, for logging and printing. A view of a string literal: no allocation, always valid.
    [[nodiscard]] constexpr std::string_view clientResponseTypeToString(ClientResponseType type) noexcept {
        switch (type) {
            case ClientResponseType::ACCEPTED:        return "ACCEPTED";
            case ClientResponseType::CANCELED:        return "CANCELED";
            case ClientResponseType::FILLED:          return "FILLED";
            case ClientResponseType::CANCEL_REJECTED: return "CANCEL_REJECTED";
            case ClientResponseType::INVALID:         return "INVALID";
        }
        return "UNKNOWN";   // a value that is none of the enumerators, e.g. a corrupted message
    }

    // One response from the exchange to one trading client (sent over TCP, copied byte for byte with memcpy).
    // Same rules as ClientRequest: largest fields first, explicit zeroed padding, everything starts invalid.
    struct ClientResponse {
        Common::OrderId clientOrderId = Common::OrderId_INVALID;    // the client's own number for the order
        Common::OrderId marketOrderId = Common::OrderId_INVALID;    // the exchange's number for it
        Common::Price price = Common::Price_INVALID;                 // FILLED: the trade price; otherwise the order's price
        Common::ClientId clientId = Common::ClientId_INVALID;        // which client this is for
        Common::TickerId tickerId = Common::TickerId_INVALID;        // which instrument
        Common::Qty execQty = Common::Qty_INVALID;                   // FILLED: quantity traded in THIS fill
        Common::Qty leavesQty = Common::Qty_INVALID;                 // quantity of the order still waiting in the book
        ClientResponseType type = ClientResponseType::INVALID;
        Common::Side side = Common::Side::INVALID;
        std::uint8_t reserved[6] = {};                               // explicit padding, always 0
    };

    static_assert(sizeof(ClientResponse) == 48);
    static_assert(std::has_unique_object_representations_v<ClientResponse>);
    static_assert(std::is_trivially_copyable_v<ClientResponse>);

}
