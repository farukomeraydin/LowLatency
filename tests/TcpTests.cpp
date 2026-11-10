#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "Common/Network.h"
#include "Common/ReceiveBuffer.h"
#include "Common/SendBuffer.h"
#include "Common/TcpConnection.h"
#include "Common/TcpServer.h"
#include "Test.h"

using namespace Common;

namespace {

    struct Message {
        std::int64_t sender;
        std::int64_t value;
    };

    std::span<const std::byte> bytesOf(const Message& message) {
        return std::as_bytes(std::span{&message, 1});
    }

    bool tryTake(TcpConnection& connection, Message& out) {
        if (connection.readable().size() < sizeof(Message)) {
            return false;
        }
        std::memcpy(&out, connection.readable().data(), sizeof(Message));
        connection.consume(sizeof(Message));
        return true;
    }

    TcpConnection connectTo(TcpServer& server) {
        Socket socket = createSocket(SocketProtocol::TCP);
        CHECK(connectSocket(socket, server.address()));
        TcpConnection client(std::move(socket), 64 * 1024, 64 * 1024);
        const std::size_t before = server.connectionCount();
        CHECK(Test::waitFor([&] {
            server.poll();
            return server.connectionCount() > before;
        }));
        return client;
    }

}

TEST(receiveBufferStartsEmpty) {
    ReceiveBuffer buffer(64);
    CHECK(buffer.capacity() == 64);
    CHECK(buffer.readable().empty());
    CHECK(buffer.freeSpace() == 64);
}

TEST(sendBufferAppendIsAllOrNothing) {
    SendBuffer buffer(16);
    const std::array<std::byte, 10> ten{};
    CHECK(buffer.append(ten));
    CHECK(buffer.pending().size() == 10);
    CHECK(!buffer.append(ten));
    CHECK(buffer.pending().size() == 10);
    CHECK(buffer.append(std::span{ten}.first(6)));
    CHECK(buffer.freeSpace() == 0);
}

TEST(tcpMessagesSurviveGluingAndSplitting) {
    WinsockSession winsock;
    TcpServer server({"127.0.0.1", 0}, 4, 64 * 1024, 64 * 1024);
    TcpConnection client = connectTo(server);

    // Many small messages in a row: TCP is free to glue or split them.
    constexpr int COUNT = 20'000;
    int sent = 0;
    int received = 0;
    int wrong = 0;
    CHECK(Test::waitFor([&] {
        while (sent < COUNT && client.send(bytesOf(Message{1, sent}))) {
            ++sent;
        }
        (void)client.poll();
        server.poll();
        Message m{};
        for (TcpConnection& connection : server.connections()) {
            while (tryTake(connection, m)) {
                wrong += (m.value != received) ? 1 : 0;
                ++received;
            }
        }
        return received == COUNT;
    }, std::chrono::milliseconds(10'000)));
    CHECK(wrong == 0);
}

TEST(tcpServerRefusesClientsBeyondItsLimit) {
    WinsockSession winsock;
    TcpServer server({"127.0.0.1", 0}, 2, 4096, 4096);

    std::vector<TcpConnection> clients;
    for (int i = 0; i < 4; ++i) {
        Socket socket = createSocket(SocketProtocol::TCP);
        CHECK(connectSocket(socket, server.address()));
        clients.emplace_back(std::move(socket), 4096, 4096);
    }
    CHECK(Test::waitFor([&] {
        server.poll();
        int closed = 0;
        for (TcpConnection& client : clients) {
            closed += client.poll() ? 0 : 1;
        }
        return closed == 2;
    }));
    CHECK(server.connectionCount() == 2);
}

TEST(tcpServerSendsByStableId) {
    WinsockSession winsock;
    TcpServer server({"127.0.0.1", 0}, 8, 4096, 4096);
    std::vector<TcpConnection> clients;
    for (int i = 0; i < 3; ++i) {
        clients.push_back(connectTo(server));
    }
    CHECK(server.connections()[0].id() == 0);
    CHECK(server.connections()[2].id() == 2);

    clients[1].close();
    CHECK(Test::waitFor([&] {
        server.poll();
        return server.connectionCount() == 2;
    }));
    clients.push_back(connectTo(server));
    CHECK(server.connections()[2].id() == 3);   // ids are never reused

    CHECK(!server.send(1, bytesOf(Message{0, 1})));
    CHECK(!server.send(99, bytesOf(Message{0, 99})));
    CHECK(server.send(2, bytesOf(Message{0, 2})));
    CHECK(server.send(3, bytesOf(Message{0, 3})));

    Message m{};
    CHECK(Test::waitFor([&] { return clients[2].poll() && tryTake(clients[2], m); }));
    CHECK(m.value == 2);
    CHECK(Test::waitFor([&] { return clients[3].poll() && tryTake(clients[3], m); }));
    CHECK(m.value == 3);
    CHECK(clients[0].poll() && clients[0].readable().empty());
}

TEST(tcpPeerCloseIsNoticed) {
    WinsockSession winsock;
    TcpServer server({"127.0.0.1", 0}, 2, 4096, 4096);
    TcpConnection client = connectTo(server);

    server.connections()[0].close();
    CHECK(Test::waitFor([&] { return !client.poll(); }));
    CHECK(!client.isOpen());
    CHECK(client.id() == ConnectionId_INVALID);
}
