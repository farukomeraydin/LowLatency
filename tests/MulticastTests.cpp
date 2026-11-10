#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "Common/Multicast.h"
#include "Common/Network.h"
#include "Common/Types.h"
#include "Test.h"

using namespace Common;

namespace {

    const std::string LOOPBACK = "127.0.0.1";

    struct Tick {
        std::uint64_t sequence;
        Price price;
    };

    std::vector<std::uint64_t> drain(McastReceiver& receiver) {
        std::vector<std::uint64_t> sequences;
        while (receiver.poll() > 0) {
            Tick tick{};
            while (receiver.readable().size() >= sizeof(Tick)) {
                std::memcpy(&tick, receiver.readable().data(), sizeof(Tick));
                receiver.consume(sizeof(Tick));
                sequences.push_back(tick.sequence);
            }
        }
        return sequences;
    }

}

TEST(multicastJoinValidatesAddresses) {
    WinsockSession winsock;
    Socket socket = createSocket(SocketProtocol::UDP);
    CHECK(bindSocket(socket, {"0.0.0.0", 0}));
    CHECK(!joinMulticastGroup(socket, "300.1.1.1", LOOPBACK));
    CHECK(!joinMulticastGroup(socket, "127.0.0.1", LOOPBACK));
    CHECK(joinMulticastGroup(socket, "239.1.1.9", LOOPBACK));
    CHECK(!setMulticastInterface(socket, "not an address"));
}

TEST(multicastReachesEveryReceiver) {
    WinsockSession winsock;
    const SocketAddress group{"239.1.1.7", 30107};
    McastReceiver first(group, LOOPBACK, 4096);
    McastReceiver second(group, LOOPBACK, 4096);
    McastSender sender(group, LOOPBACK);

    for (std::uint64_t sequence = 1; sequence <= 50; ++sequence) {
        const Tick tick{sequence, 15000};
        CHECK(sender.send(std::as_bytes(std::span{&tick, 1})));
    }

    std::vector<std::uint64_t> gotFirst;
    std::vector<std::uint64_t> gotSecond;
    CHECK(Test::waitFor([&] {
        for (std::uint64_t s : drain(first)) gotFirst.push_back(s);
        for (std::uint64_t s : drain(second)) gotSecond.push_back(s);
        return gotFirst.size() == 50 && gotSecond.size() == 50;
    }));
    for (std::uint64_t i = 0; i < gotFirst.size() && i < gotSecond.size(); ++i) {
        CHECK(gotFirst[i] == i + 1);
        CHECK(gotSecond[i] == i + 1);
    }
}

TEST(multicastRejectsOversizedPackets) {
    WinsockSession winsock;
    const SocketAddress group{"239.1.1.8", 30108};
    McastReceiver receiver(group, LOOPBACK, 4096);
    McastSender sender(group, LOOPBACK);
    std::vector<std::byte> huge(70'000);
    CHECK(!sender.send(huge));
}
