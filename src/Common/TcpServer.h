#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <utility>
#include <vector>

#include "Assert.h"
#include "Network.h"
#include "TcpConnection.h"

namespace Common {

    // A TCP server: listens on one address and keeps one TcpConnection per connected client.
    // Like TcpConnection, it never waits: the owner calls poll() once per round of its main loop.
    class TcpServer {
    public:
        // Starts listening on `address` (port 0 = any free port, see address()). Accepts at most
        // `maxConnections` clients at a time; every connection gets buffers of the given sizes.
        // Stops the program if it cannot listen: a server that cannot listen is useless.
        TcpServer(const SocketAddress& address, std::size_t maxConnections, std::size_t receiveCapacity,
                  std::size_t sendCapacity)
            : listener_(createSocket(SocketProtocol::TCP)),
              maxConnections_(maxConnections),
              receiveCapacity_(receiveCapacity),
              sendCapacity_(sendCapacity) {
            if (maxConnections == 0) {
                fatal("TcpServer: maxConnections must be greater than 0");
            }
            if (!bindSocket(listener_, address) || !listenSocket(listener_, LISTEN_BACKLOG) || !setNonBlocking(listener_)) {
                fatal(std::format("TcpServer: cannot listen on {}:{}", address.ip, address.port));
            }
            connections_.reserve(maxConnections);   // the vector never has to grow (reallocate) later
        }

        // The address and port the server listens on.
        [[nodiscard]] SocketAddress address() const { return getLocalAddress(listener_); }

        [[nodiscard]] std::size_t connectionCount() const noexcept { return connections_.size(); }

        // The server's network work for one round of the main loop. Never waits:
        //   1. accepts new clients,
        //   2. polls every connection (sends pending bytes, receives new ones),
        //   3. removes the connections that are closed now.
        // Afterwards, handle the whole messages in each of connections().
        void poll() {
            acceptNewConnections();
            for (TcpConnection& connection : connections_) {
                (void)connection.poll();   // false = closed: removed just below
            }
            std::erase_if(connections_, [](const TcpConnection& connection) { return !connection.isOpen(); });
        }

        // The open connections, to read their messages and send replies. Valid until the next poll()
        // (which may add and remove connections).
        [[nodiscard]] std::span<TcpConnection> connections() noexcept { return connections_; }

        // Sends one whole message to the connection with this id, e.g. a saved id, long after its message arrived.
        // Returns false if that client is gone (or its connection closes now, or its send buffer is full).
        [[nodiscard]] bool send(ConnectionId id, std::span<const std::byte> message) noexcept {
            // connections_ is always sorted by id: ids are given in increasing order to push_back, and
            // erase_if keeps the order of the rest. So a binary search finds the id.
            const auto found = std::lower_bound(connections_.begin(), connections_.end(), id,
                [](const TcpConnection& connection, ConnectionId wanted) { return connection.id() < wanted; });
            if (found == connections_.end() || found->id() != id) {
                return false;   // that client disconnected (and was removed)
            }
            return found->send(message);
        }

    private:
        // Accepts every client waiting in the listen queue. Clients beyond maxConnections are refused:
        // their socket is closed at once, so they see the connection closed.
        void acceptNewConnections() {
            while (true) {
                AcceptResult accepted = tryAccept(listener_);
                if (accepted.status != IoStatus::OK) {
                    return;   // WOULD_BLOCK: nobody is waiting (ERR: try again next round)
                }
                if (connections_.size() >= maxConnections_) {
                    continue;   // full: accepted.socket is destroyed at the end of this round, which closes it
                }
                TcpConnection connection(std::move(accepted.socket), receiveCapacity_, sendCapacity_, nextConnectionId_);
                if (connection.isOpen()) {
                    connections_.push_back(std::move(connection));
                    ++nextConnectionId_;   // never reused (64 bits: wrapping around would take centuries)
                }
            }
        }

        static constexpr int LISTEN_BACKLOG = 64;   // clients that may wait in the OS queue between two polls

        Socket listener_;
        std::size_t maxConnections_;
        std::size_t receiveCapacity_;
        std::size_t sendCapacity_;
        std::vector<TcpConnection> connections_;
        ConnectionId nextConnectionId_ = 0;   // the id the next accepted client gets
    };

}
