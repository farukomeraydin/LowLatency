#pragma once

#include <cstddef>
#include <format>
#include <span>
#include <string>

#include "Assert.h"
#include "Network.h"
#include "ReceiveBuffer.h"

namespace Common {

    // Sends UDP multicast messages to one group (e.g. the exchange publishing market data).
    // Each send() is one whole message (one UDP packet). Nothing is buffered or retried: a message the OS
    // cannot take right now is dropped, and receivers notice the gap through sequence numbers.
    // Copying is not possible (Socket cannot be copied); moving is (rule of zero).
    class McastSender {
    public:
        // `group` = group address and port, e.g. {"239.1.1.1", 30001}. `interfaceIp` = the network interface to
        // send from (receivers must join on the same one; "127.0.0.1" = inside this computer).
        // Stops the program if the socket cannot be set up: a publisher that cannot publish is useless.
        McastSender(const SocketAddress& group, const std::string& interfaceIp)
            : socket_(createSocket(SocketProtocol::UDP)) {
            if (!setNonBlocking(socket_) || !setMulticastInterface(socket_, interfaceIp) || !connectSocket(socket_, group)) {
                fatal(std::format("McastSender: cannot send to {}:{} via {}", group.ip, group.port, interfaceIp));
            }
        }

        // Sends one whole message to the group. Never waits.
        // Returns false if it was not sent: the OS is busy, the message is too big for one UDP packet, or
        // (Windows, loopback interface only) no socket on this computer has joined the group yet.
        [[nodiscard]] bool send(std::span<const std::byte> message) noexcept {
            const IoResult result = trySend(socket_, message);
            return result.status == IoStatus::OK && result.bytes == message.size();
        }

    private:
        Socket socket_;
    };

    // Receives UDP multicast messages from one group (e.g. a trading client receiving market data).
    // Packets are collected in a ReceiveBuffer; cut whole messages from readable(), then consume() them,
    // exactly as with TcpConnection. A lost packet means whole messages are missing (never half of one):
    // use the messages' sequence numbers to notice.
    // Copying is not possible; moving is (rule of zero).
    class McastReceiver {
    public:
        // Largest packet we promise to receive whole: one normal network frame.
        static constexpr std::size_t MAX_PACKET_SIZE = 1500;

        // Joins `group` (address and port) on the network interface `interfaceIp` (the sender's interface).
        // Several receivers on this computer may join the same group. `receiveCapacity` must be at least
        // 2 * MAX_PACKET_SIZE. Stops the program if the socket cannot be set up (startup).
        McastReceiver(const SocketAddress& group, const std::string& interfaceIp, std::size_t receiveCapacity)
            : socket_(createSocket(SocketProtocol::UDP)), receiveBuffer_(receiveCapacity) {
            if (receiveCapacity < 2 * MAX_PACKET_SIZE) {
                fatal(std::format("McastReceiver: receiveCapacity must be at least {}", 2 * MAX_PACKET_SIZE));
            }
            if (!setReuseAddress(socket_) || !bindSocket(socket_, {"0.0.0.0", group.port}) || !setNonBlocking(socket_) ||
                !joinMulticastGroup(socket_, group.ip, interfaceIp)) {
                fatal(std::format("McastReceiver: cannot join {}:{} via {}", group.ip, group.port, interfaceIp));
            }
        }

        // This receiver's network work for one round of the main loop. Never waits.
        // Receives every packet that has arrived, while the buffer surely has room for one more whole packet.
        // Returns how many packets were received.
        std::size_t poll() noexcept {
            std::size_t packets = 0;
            // Room after compaction = capacity - readable. With capacity >= 2 * MAX_PACKET_SIZE, receiveFrom()
            // compacts whenever the free space at the end is smaller than that, so a whole packet always fits.
            while (receiveBuffer_.capacity() - receiveBuffer_.readable().size() >= MAX_PACKET_SIZE) {
                const IoResult result = receiveBuffer_.receiveFrom(socket_);
                if (result.status != IoStatus::OK) {
                    break;   // WOULD_BLOCK: nothing more has arrived (ERR: e.g. a packet too big; it is lost)
                }
                ++packets;
            }
            return packets;
        }

        // The received bytes not yet consumed: cut whole messages from the front, then consume() them.
        [[nodiscard]] std::span<const std::byte> readable() const noexcept { return receiveBuffer_.readable(); }

        void consume(std::size_t count) noexcept { receiveBuffer_.consume(count); }

    private:
        Socket socket_;
        ReceiveBuffer receiveBuffer_;
    };

}
