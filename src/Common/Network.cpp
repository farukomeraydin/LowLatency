#include "Network.h"

#include <format>

#define WIN32_LEAN_AND_MEAN   // leave out rarely used parts of windows.h (and the old winsock.h)
#define NOMINMAX              // stop windows.h from defining min/max macros
#include <winsock2.h>         // Winsock 2: must come before windows.h
#include <ws2tcpip.h>         // newer Winsock functions (inet_pton, getaddrinfo, ...)

#include "Assert.h"

// Tell the linker to add Winsock's library. Without it, every Winsock function would be an
// "unresolved external symbol" error.
#pragma comment(lib, "Ws2_32.lib")

namespace Common {

    // Our portable SocketHandle must be able to hold every Winsock SOCKET value.
    static_assert(sizeof(SocketHandle) == sizeof(SOCKET));
    static_assert(INVALID_SOCKET_HANDLE == INVALID_SOCKET);

    WinsockSession::WinsockSession() {
        WSADATA details{};
        const int result = WSAStartup(MAKEWORD(2, 2), &details);   // ask for Winsock version 2.2
        if (result != 0) {
            fatal(std::format("WinsockSession: WSAStartup failed with error {}", result));
        }
    }

    WinsockSession::~WinsockSession() {
        WSACleanup();
    }

    std::string getHostName() {
        char name[256]{};
        if (gethostname(name, static_cast<int>(sizeof(name))) != 0) {
            return {};
        }
        return name;
    }

    void Socket::close() noexcept {
        if (isValid()) {
            closesocket(static_cast<SOCKET>(handle_));
            handle_ = INVALID_SOCKET_HANDLE;
        }
    }

    Socket createSocket(SocketProtocol protocol) {
        const bool isTcp = (protocol == SocketProtocol::TCP);
        const SOCKET handle = socket(AF_INET,                              // IPv4 addresses
                                     isTcp ? SOCK_STREAM : SOCK_DGRAM,     // byte stream or messages
                                     isTcp ? IPPROTO_TCP : IPPROTO_UDP);
        if (handle == INVALID_SOCKET) {
            fatal(std::format("createSocket: socket() failed with error {}", WSAGetLastError()));
        }
        return Socket{static_cast<SocketHandle>(handle)};
    }

    bool setNonBlocking(Socket& socket) noexcept {
        u_long nonBlocking = 1;   // 1 = non-blocking, 0 = blocking
        return ioctlsocket(static_cast<SOCKET>(socket.handle()), FIONBIO, &nonBlocking) == 0;
    }

    bool setNoDelay(Socket& socket) noexcept {
        const BOOL noDelay = TRUE;
        return setsockopt(static_cast<SOCKET>(socket.handle()), IPPROTO_TCP, TCP_NODELAY,
                          reinterpret_cast<const char*>(&noDelay), static_cast<int>(sizeof(noDelay))) == 0;
    }

    bool isNoDelay(const Socket& socket) noexcept {
        BOOL noDelay = FALSE;
        int size = static_cast<int>(sizeof(noDelay));
        if (getsockopt(static_cast<SOCKET>(socket.handle()), IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<char*>(&noDelay), &size) != 0) {
            return false;
        }
        return noDelay != FALSE;
    }

    namespace {

        // Our SocketAddress -> Winsock's sockaddr_in. The IP text becomes 4 bytes and the port is put
        // into network byte order (big-endian). Returns false if the IP text is not a valid IPv4 address.
        bool toSockaddr(const SocketAddress& address, sockaddr_in& out) noexcept {
            out = sockaddr_in{};
            out.sin_family = AF_INET;
            out.sin_port = htons(address.port);
            return inet_pton(AF_INET, address.ip.c_str(), &out.sin_addr) == 1;
        }

        // Winsock's sockaddr_in -> our SocketAddress (the reverse of toSockaddr).
        SocketAddress fromSockaddr(const sockaddr_in& in) {
            char text[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &in.sin_addr, text, sizeof(text));
            return SocketAddress{text, ntohs(in.sin_port)};
        }

    }

    bool bindSocket(Socket& socket, const SocketAddress& address) {
        sockaddr_in native{};
        if (!toSockaddr(address, native)) {
            return false;
        }
        return bind(static_cast<SOCKET>(socket.handle()), reinterpret_cast<const sockaddr*>(&native),
                    static_cast<int>(sizeof(native))) == 0;
    }

    bool connectSocket(Socket& socket, const SocketAddress& address) {
        sockaddr_in native{};
        if (!toSockaddr(address, native)) {
            return false;
        }
        if (connect(static_cast<SOCKET>(socket.handle()), reinterpret_cast<const sockaddr*>(&native),
                    static_cast<int>(sizeof(native))) == 0) {
            return true;
        }
        // A non-blocking TCP socket cannot finish connecting at once: "would block" means "started".
        return WSAGetLastError() == WSAEWOULDBLOCK;
    }

    namespace {

        // Turns the return value of send()/recv() into an IoResult.
        IoResult toIoResult(int result) noexcept {
            if (result > 0) {
                return {IoStatus::OK, static_cast<std::size_t>(result)};
            }
            if (result == 0) {
                return {IoStatus::CLOSED, 0};   // recv() returns 0 when a TCP peer closed the connection
            }
            const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                return {IoStatus::WOULD_BLOCK, 0};
            }
            return {IoStatus::ERR, 0};
        }

    }

    IoResult trySend(Socket& socket, std::span<const std::byte> data) noexcept {
        const int result = send(static_cast<SOCKET>(socket.handle()), reinterpret_cast<const char*>(data.data()),
                                static_cast<int>(data.size()), 0);
        return toIoResult(result);
    }

    IoResult tryReceive(Socket& socket, std::span<std::byte> buffer) noexcept {
        const int result = recv(static_cast<SOCKET>(socket.handle()), reinterpret_cast<char*>(buffer.data()),
                                static_cast<int>(buffer.size()), 0);
        return toIoResult(result);
    }

    bool listenSocket(Socket& socket, int backlog) noexcept {
        return listen(static_cast<SOCKET>(socket.handle()), backlog) == 0;
    }

    AcceptResult tryAccept(Socket& listener) noexcept {
        // nullptr, nullptr: we don't ask for the client's address here (getpeername can tell it later).
        const SOCKET handle = accept(static_cast<SOCKET>(listener.handle()), nullptr, nullptr);
        if (handle != INVALID_SOCKET) {
            return {IoStatus::OK, Socket{static_cast<SocketHandle>(handle)}};
        }
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            return {IoStatus::WOULD_BLOCK, Socket{}};
        }
        return {IoStatus::ERR, Socket{}};
    }

    IoStatus tryFinishConnect(Socket& socket) noexcept {
        const SOCKET handle = static_cast<SOCKET>(socket.handle());

        // select() asks the OS about a set of sockets. We ask about just one, in two sets:
        //   "writable":  on Windows, a pending connect that SUCCEEDED makes the socket writable,
        //   "exception": on Windows, a pending connect that FAILED reports the socket here.
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(handle, &writable);
        fd_set failed;
        FD_ZERO(&failed);
        FD_SET(handle, &failed);

        timeval noWait{0, 0};   // timeout 0: just look and return, never sleep
        const int ready = select(0, nullptr, &writable, &failed, &noWait);   // 1st argument is ignored on Windows
        if (ready == 0) {
            return IoStatus::WOULD_BLOCK;   // nothing happened yet: the handshake is still going on
        }
        if (ready > 0 && FD_ISSET(handle, &writable)) {
            return IoStatus::OK;
        }
        return IoStatus::ERR;   // in the "failed" set, or select() itself failed
    }

    bool joinMulticastGroup(Socket& socket, const std::string& groupIp, const std::string& interfaceIp) {
        ip_mreq membership{};   // "membership request": which group, on which interface
        if (inet_pton(AF_INET, groupIp.c_str(), &membership.imr_multiaddr) != 1 ||
            inet_pton(AF_INET, interfaceIp.c_str(), &membership.imr_interface) != 1) {
            return false;
        }
        return setsockopt(static_cast<SOCKET>(socket.handle()), IPPROTO_IP, IP_ADD_MEMBERSHIP,
                          reinterpret_cast<const char*>(&membership), static_cast<int>(sizeof(membership))) == 0;
    }

    bool setMulticastInterface(Socket& socket, const std::string& interfaceIp) {
        in_addr interfaceAddress{};
        if (inet_pton(AF_INET, interfaceIp.c_str(), &interfaceAddress) != 1) {
            return false;
        }
        return setsockopt(static_cast<SOCKET>(socket.handle()), IPPROTO_IP, IP_MULTICAST_IF,
                          reinterpret_cast<const char*>(&interfaceAddress), static_cast<int>(sizeof(interfaceAddress))) == 0;
    }

    bool setReuseAddress(Socket& socket) noexcept {
        const BOOL reuse = TRUE;
        return setsockopt(static_cast<SOCKET>(socket.handle()), SOL_SOCKET, SO_REUSEADDR,
                          reinterpret_cast<const char*>(&reuse), static_cast<int>(sizeof(reuse))) == 0;
    }

    SocketAddress getLocalAddress(const Socket& socket) {
        sockaddr_in native{};
        int size = static_cast<int>(sizeof(native));
        if (getsockname(static_cast<SOCKET>(socket.handle()), reinterpret_cast<sockaddr*>(&native), &size) != 0) {
            return {};
        }
        return fromSockaddr(native);
    }

}
