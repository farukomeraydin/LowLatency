#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include "Network.h"
#include "ReceiveBuffer.h"
#include "SendBuffer.h"

namespace Common {

    // Identity of one connection on a TcpServer. Given out in order (0, 1, 2, ...) and never reused,
    // so an id saved earlier can never point to a different, newer client.
    using ConnectionId = std::uint64_t;
    inline constexpr ConnectionId ConnectionId_INVALID = std::numeric_limits<ConnectionId>::max();

    // One open TCP connection: the socket plus its own receive and send buffers.
    // Used on both sides: the exchange holds one per connected client, a trading client holds one to the exchange.
    // Copying is not possible (its members cannot be copied); moving is (rule of zero: no special members written).
    class TcpConnection {
    public:
        // Takes ownership of an already connected socket and makes it non-blocking with Nagle off.
        // If the socket is invalid or the OS refuses the settings, the socket is closed: check isOpen().
        // `id` is given by a TcpServer; a client's own connection to the exchange doesn't need one.
        TcpConnection(Socket socket, std::size_t receiveCapacity, std::size_t sendCapacity,
                      ConnectionId id = ConnectionId_INVALID)
            : socket_(std::move(socket)), receiveBuffer_(receiveCapacity), sendBuffer_(sendCapacity), id_(id) {
            if (!setNonBlocking(socket_) || !setNoDelay(socket_)) {
                socket_.close();
            }
        }

        [[nodiscard]] ConnectionId id() const noexcept { return id_; }

        [[nodiscard]] bool isOpen() const noexcept { return socket_.isValid(); }

        // Closes the socket (if open). The buffers stay allocated. Safe to call more than once.
        void close() noexcept { socket_.close(); }

        [[nodiscard]] const Socket& socket() const noexcept { return socket_; }

        // This connection's network work for one round of the program's main loop. Never waits:
        //   1. sends pending bytes (if any) as far as the OS takes them,
        //   2. receives whatever has arrived into the receive buffer (then see readable()).
        // If the other side closed the connection or an error happened, the connection is closed.
        // Returns false if the connection is not open (any more).
        [[nodiscard]] bool poll() noexcept {
            if (!isOpen()) {
                return false;
            }
            if (!flush()) {
                return false;
            }
            // A completely full receive buffer is not an error: the caller is behind with consume().
            // Skip receiving; the bytes wait in the OS, and TCP flow control slows the sender down.
            if (receiveBuffer_.readable().size() < receiveBuffer_.capacity()) {
                const IoResult result = receiveBuffer_.receiveFrom(socket_);
                if (result.status == IoStatus::CLOSED || result.status == IoStatus::ERR) {
                    close();
                    return false;
                }
            }
            return true;
        }

        // Queues one whole message and tries to send it right away.
        // Returns false if the connection is not open (any more) or the send buffer has no room for it.
        [[nodiscard]] bool send(std::span<const std::byte> message) noexcept {
            if (!isOpen() || !sendBuffer_.append(message)) {
                return false;
            }
            return flush();
        }

        // The received bytes not yet consumed: cut whole messages from the front, then consume() them.
        [[nodiscard]] std::span<const std::byte> readable() const noexcept { return receiveBuffer_.readable(); }

        void consume(std::size_t count) noexcept { receiveBuffer_.consume(count); }

    private:
        // Sends pending bytes; closes the connection on CLOSED/ERR. Returns false if it was closed.
        [[nodiscard]] bool flush() noexcept {
            const IoResult result = sendBuffer_.sendTo(socket_);
            if (result.status == IoStatus::CLOSED || result.status == IoStatus::ERR) {
                close();
                return false;
            }
            return true;
        }

        Socket socket_;
        ReceiveBuffer receiveBuffer_;
        SendBuffer sendBuffer_;
        ConnectionId id_;
    };

}
