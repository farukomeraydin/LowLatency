#include <array>
#include <cstring>
#include <string>

#include "Common/MemoryPool.h"
#include "Exchange/ClientRequest.h"
#include "Exchange/ClientResponse.h"
#include "Exchange/MarketUpdate.h"
#include "Exchange/Order.h"
#include "Test.h"

using namespace Common;
using namespace Exchange;

namespace {

    template <typename T>
    bool roundTrips(const T& message) {
        std::array<std::byte, sizeof(T)> wire{};
        std::memcpy(wire.data(), &message, sizeof(T));
        T decoded{};
        std::memcpy(&decoded, wire.data(), sizeof(T));
        return std::memcmp(&message, &decoded, sizeof(T)) == 0;
    }

}

TEST(messagesHaveTheirWireSizes) {
    CHECK(sizeof(ClientRequest) == 32);
    CHECK(sizeof(ClientResponse) == 48);
    CHECK(sizeof(MarketUpdate) == 32);
    CHECK(sizeof(Order) == 64);
}

TEST(messagesStartInvalid) {
    const ClientRequest request{};
    CHECK(request.type == ClientRequestType::INVALID);
    CHECK(request.clientId == ClientId_INVALID);
    CHECK(request.price == Price_INVALID);
    CHECK(request.side == Side::INVALID);

    const ClientResponse response{};
    CHECK(response.type == ClientResponseType::INVALID);
    CHECK(response.leavesQty == Qty_INVALID);

    const MarketUpdate update{};
    CHECK(update.type == MarketUpdateType::INVALID);
    CHECK(update.priority == Priority_INVALID);
}

TEST(messageTypeNames) {
    CHECK(clientRequestTypeToString(ClientRequestType::CANCEL) == "CANCEL");
    CHECK(clientResponseTypeToString(ClientResponseType::CANCEL_REJECTED) == "CANCEL_REJECTED");
    CHECK(marketUpdateTypeToString(MarketUpdateType::MODIFY) == "MODIFY");
    CHECK(clientRequestTypeToString(static_cast<ClientRequestType>(9)) == "UNKNOWN");
    CHECK(marketUpdateTypeToString(static_cast<MarketUpdateType>(9)) == "UNKNOWN");
}

TEST(messagesRoundTripThroughBytes) {
    CHECK(roundTrips(ClientRequest{.clientOrderId = 7, .price = 15029, .clientId = 4, .tickerId = 0, .qty = 250,
                                   .type = ClientRequestType::NEW, .side = Side::BUY}));
    CHECK(roundTrips(ClientResponse{.clientOrderId = 7, .marketOrderId = 4, .price = 15028, .clientId = 4,
                                    .tickerId = 0, .execQty = 200, .leavesQty = 50,
                                    .type = ClientResponseType::FILLED, .side = Side::BUY}));
    CHECK(roundTrips(MarketUpdate{.marketOrderId = 2, .price = 15028, .tickerId = 0, .qty = 50, .priority = 2,
                                  .type = MarketUpdateType::MODIFY, .side = Side::SELL}));
}

TEST(ordersLinkIntoACircle) {
    MemoryPool<Order> pool(4);
    Order* a = pool.allocate(TickerId{0}, ClientId{1}, OrderId{11}, OrderId{1}, Side::SELL, Price{15028}, Qty{200}, Priority{1});
    Order* b = pool.allocate(TickerId{0}, ClientId{2}, OrderId{21}, OrderId{2}, Side::SELL, Price{15028}, Qty{100}, Priority{2});
    Order* c = pool.allocate(TickerId{0}, ClientId{3}, OrderId{31}, OrderId{3}, Side::SELL, Price{15028}, Qty{300}, Priority{3});
    CHECK(a->prev == nullptr && a->next == nullptr);
    CHECK(b->clientOrderId == 21 && b->qty == 100 && b->priority == 2);

    a->next = b; b->next = c; c->next = a;
    a->prev = c; b->prev = a; c->prev = b;

    b->prev->next = b->next;
    b->next->prev = b->prev;
    pool.deallocate(b);

    CHECK(a->next == c && c->next == a);
    CHECK(a->prev == c && c->prev == a);
    CHECK(orderToString(*a).find("prev 3 next 3") != std::string::npos);

    pool.deallocate(a);
    pool.deallocate(c);
}
