#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace Common {

    // Starts the Windows socket library (Winsock) when constructed and shuts it down when destroyed.
    // Create one at the start of main(), before any network function is used, and keep it alive
    // until all networking is finished.
    class WinsockSession {
    public:
        WinsockSession();
        ~WinsockSession();

        WinsockSession(const WinsockSession&) = delete;
        WinsockSession& operator=(const WinsockSession&) = delete;
        WinsockSession(WinsockSession&&) = delete;
        WinsockSession& operator=(WinsockSession&&) = delete;
    };

    // The name of this computer on the network, or an empty string if it cannot be read
    // (for example because no WinsockSession exists).
    [[nodiscard]] std::string getHostName();

    // The operating system's number for one socket (on Windows a SOCKET is a pointer-sized unsigned integer).
    using SocketHandle = std::uintptr_t;

    // "No socket" marker; the same value as Winsock's INVALID_SOCKET (checked in Network.cpp).
    inline constexpr SocketHandle INVALID_SOCKET_HANDLE = ~SocketHandle{0};

    // Owns one socket and closes it when destroyed (RAII). Move-only, like std::unique_ptr:
    // moving hands the socket to another Socket object, copying is not allowed.
    class Socket {
    public:
        Socket() noexcept = default;   // owns no socket
        explicit Socket(SocketHandle handle) noexcept : handle_(handle) {}
        ~Socket() { close(); }

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        Socket(Socket&& other) noexcept
            : handle_(std::exchange(other.handle_, INVALID_SOCKET_HANDLE)) {}

        Socket& operator=(Socket&& other) noexcept {
            if (this != &other) {
                close();   // give up the socket we own now, if any
                handle_ = std::exchange(other.handle_, INVALID_SOCKET_HANDLE);
            }
            return *this;
        }

        [[nodiscard]] SocketHandle handle() const noexcept { return handle_; }
        [[nodiscard]] bool isValid() const noexcept { return handle_ != INVALID_SOCKET_HANDLE; }

        // Closes the socket now (if there is one). Safe to call more than once.
        void close() noexcept;

    private:
        SocketHandle handle_ = INVALID_SOCKET_HANDLE;
    };

    enum class SocketProtocol : std::uint8_t {
        TCP,   // connected, reliable byte stream (orders)
        UDP    // separate messages, no guarantees (market data)
    };

    // Creates a new IPv4 socket for the given protocol. Stops the program if the OS refuses.
    [[nodiscard]] Socket createSocket(SocketProtocol protocol);

    // Makes send/receive/connect/accept return immediately instead of putting the thread to sleep
    // while they wait. Returns false if the OS refuses.
    [[nodiscard]] bool setNonBlocking(Socket& socket) noexcept;

    // TCP only: turns off Nagle's algorithm, so small messages are sent at once instead of being
    // held back to be combined with later data. Returns false if the OS refuses (e.g. on a UDP socket).
    [[nodiscard]] bool setNoDelay(Socket& socket) noexcept;

    // TCP only: true if Nagle's algorithm is turned off on this socket.
    [[nodiscard]] bool isNoDelay(const Socket& socket) noexcept;

    // An IPv4 address and a port, in normal human form: e.g. {"127.0.0.1", 12345}.
    // Port 0 in bindSocket() means "let the operating system pick a free port".
    struct SocketAddress {
        std::string ip;
        std::uint16_t port = 0;
    };

    // Attaches the socket to a local address and port, so that data sent to that address reaches it.
    // Returns false if the OS refuses (bad address, or the port is already used by another socket).
    [[nodiscard]] bool bindSocket(Socket& socket, const SocketAddress& address);

    // The local address and port the socket is bound to ({"", 0} if it is not bound).
    [[nodiscard]] SocketAddress getLocalAddress(const Socket& socket);

    // Sets the remote address this socket talks to.
    //   UDP: done immediately; afterwards trySend() sends there and tryReceive() accepts only data from there.
    //   TCP: starts opening a connection (finish it with tryFinishConnect()).
    // Returns false if the OS refuses.
    [[nodiscard]] bool connectSocket(Socket& socket, const SocketAddress& address);

    // What happened in one trySend()/tryReceive() call.
    enum class IoStatus : std::uint8_t {
        OK,            // bytes were sent / received (see IoResult::bytes)
        WOULD_BLOCK,   // non-blocking socket: nothing could be done right now, try again later
        CLOSED,        // TCP: the other side closed the connection
        ERR            // something went wrong (ERR, not ERROR: windows.h defines a macro ERROR)
    };

    struct IoResult {
        IoStatus status = IoStatus::ERR;
        std::size_t bytes = 0;   // how many bytes were sent / received when status is OK
    };

    // Sends data to the connected address. On a non-blocking socket it never waits.
    [[nodiscard]] IoResult trySend(Socket& socket, std::span<const std::byte> data) noexcept;

    // Receives whatever data has arrived, up to buffer.size() bytes. On a non-blocking socket it never
    // waits: with nothing to read it returns WOULD_BLOCK. (UDP: one call = one whole message.)
    [[nodiscard]] IoResult tryReceive(Socket& socket, std::span<std::byte> buffer) noexcept;

    // TCP server side: turns a bound socket into a "listening" socket that accepts incoming connections.
    // Up to `backlog` connections that are fully opened but not yet accepted wait in a queue in the OS.
    // Returns false if the OS refuses (e.g. the socket is UDP or not bound).
    [[nodiscard]] bool listenSocket(Socket& socket, int backlog) noexcept;

    // What happened in one tryAccept() call. On OK, `socket` is a NEW socket connected to one client;
    // the listening socket stays as it is and keeps accepting further clients.
    struct AcceptResult {
        IoStatus status = IoStatus::ERR;   // OK, WOULD_BLOCK (no client waiting) or ERR
        Socket socket;                     // valid only when status is OK
    };

    // Takes the next waiting connection from the listening socket's queue. On a non-blocking listening
    // socket it never waits: with no client waiting it returns WOULD_BLOCK.
    // The new socket inherits the listening socket's settings (e.g. non-blocking).
    [[nodiscard]] AcceptResult tryAccept(Socket& listener) noexcept;

    // TCP client side, after connectSocket() on a non-blocking socket: has the connection finished opening?
    // Never waits. Returns OK (connected, ready to send), WOULD_BLOCK (still connecting, ask again later)
    // or ERR (the connection failed, e.g. nobody is listening at that address).
    [[nodiscard]] IoStatus tryFinishConnect(Socket& socket) noexcept;

    // UDP multicast receiver: asks the OS to deliver messages sent to the group address `groupIp`
    // (224.0.0.0 - 239.255.255.255) to this socket. Bind the socket to the group's port first.
    // `interfaceIp` picks the network interface (network card) to listen on: "0.0.0.0" lets the OS choose.
    // The membership ends when the socket is closed. Returns false if an address is invalid or the OS refuses.
    [[nodiscard]] bool joinMulticastGroup(Socket& socket, const std::string& groupIp, const std::string& interfaceIp);

    // UDP multicast sender: sends this socket's multicast messages out of the network interface with
    // this IP address (e.g. "127.0.0.1" to stay inside this computer). Receivers must have joined on the
    // same interface. Without this, the OS picks its default interface. Returns false if the address is
    // invalid or the OS refuses.
    [[nodiscard]] bool setMulticastInterface(Socket& socket, const std::string& interfaceIp);

    // Allows several sockets on this computer to bind the same port, e.g. several multicast receivers of the
    // same market data feed. Must be called BEFORE bindSocket(), on every socket that shares the port.
    // Returns false if the OS refuses.
    [[nodiscard]] bool setReuseAddress(Socket& socket) noexcept;

}
