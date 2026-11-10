#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "../Common/Types.h"

namespace Exchange {

    // What a trading client asks the exchange to do with an order.
    // One byte, because it travels in every request message. INVALID = 0, so a message that was only
    // zero-initialized (and never filled in) can never be mistaken for a real request.
    enum class ClientRequestType : std::uint8_t {
        INVALID = 0,
        NEW     = 1,   // place a new order
        CANCEL  = 2    // cancel a resting order
    };

    // Text form, for logging and printing. A view of a string literal: no allocation, always valid.
    [[nodiscard]] constexpr std::string_view clientRequestTypeToString(ClientRequestType type) noexcept {
        switch (type) {
            case ClientRequestType::NEW:     return "NEW";
            case ClientRequestType::CANCEL:  return "CANCEL";
            case ClientRequestType::INVALID: return "INVALID";
        }
        return "UNKNOWN";   // a value that is none of the enumerators, e.g. a corrupted message
    }

    // One request from a trading client to the exchange (sent over TCP, copied byte for byte with memcpy).
    // Fields are ordered from largest to smallest, so the compiler needs no padding between them; the last
    // 2 bytes are padding we write ourselves, so EVERY byte of the message has a defined value (zero).
    // Every field starts as "invalid": a request that was not filled in completely is easy to reject.
    struct ClientRequest {
        Common::OrderId clientOrderId = Common::OrderId_INVALID;    // the client's own number for the order
        Common::Price price = Common::Price_INVALID;                 // limit price in ticks (NEW only)
        Common::ClientId clientId = Common::ClientId_INVALID;        // who sends the request
        Common::TickerId tickerId = Common::TickerId_INVALID;        // which instrument
        Common::Qty qty = Common::Qty_INVALID;                       // quantity (NEW only)
        ClientRequestType type = ClientRequestType::INVALID;         // NEW or CANCEL
        Common::Side side = Common::Side::INVALID;                   // BUY or SELL
        std::uint8_t reserved[2] = {};                               // explicit padding, always 0
    };

    // Checked by the compiler: the size we designed (two messages per 64-byte cache line), no hidden padding
    // (every byte belongs to a field), and safe to copy with memcpy.
    static_assert(sizeof(ClientRequest) == 32);
    static_assert(std::has_unique_object_representations_v<ClientRequest>);
    static_assert(std::is_trivially_copyable_v<ClientRequest>);

}
