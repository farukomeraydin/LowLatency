# LowLatency

A low-latency trading system written from scratch in C++20: an exchange simulator (matching engine, order
gateway, market data publisher) and, later, a trading client that connects to it. The building blocks are
written by hand instead of taken from libraries, so that every allocation, thread hop and system call on the
hot path is known and measured.

Windows / MSVC only for now (Winsock, `SetThreadAffinityMask`).

## Status

Work in progress. The low-latency building blocks are done; the exchange simulator is being built.

| Area | State |
|---|---|
| Core types, asserts, clocks | done |
| Thread pinning | done |
| Memory pool | done |
| Lock-free SPSC queue | done |
| Asynchronous logger | done |
| TCP (connection, buffers, multi-client server) | done |
| UDP multicast (sender, receiver) | done |
| Exchange message types (request, response, market update) | done |
| Order book | in progress (`Order` done; price levels, book, matching next) |
| Matching engine, order gateway, market data publisher | planned |
| Trading client (market data consumer, strategies, risk, positions) | planned |
| Benchmarks, latency histograms, replay | planned |

## Layout

```
src/
  Common/      building blocks with no trading logic
    Types.h          ids, prices (integer ticks), quantities, Side, INVALID sentinels
    Assert.h         fatal(), assertTrue() (Debug only)
    Time.h           steady and wall clock in nanoseconds
    Thread.h/.cpp    pin a thread to a core, start pinned threads
    MemoryPool.h     fixed-capacity object pool with a free list
    LFQueue.h        single-producer/single-consumer lock-free ring buffer
    Logger.h         async logger: hot path pushes a fixed-size record, a background thread formats it
    Network.h/.cpp   RAII Winsock session and sockets, non-blocking I/O, TCP/UDP/multicast options
    ReceiveBuffer.h  byte buffer for cutting whole messages out of a TCP stream
    SendBuffer.h     byte buffer that keeps unsent bytes when the OS is busy
    TcpConnection.h  socket + both buffers, polled from the owner's loop
    TcpServer.h      listening socket + connections with stable ids
    Multicast.h      McastSender / McastReceiver
  Exchange/    exchange-side code
    ClientRequest.h  NEW / CANCEL request (32 bytes)
    ClientResponse.h ACCEPTED / CANCELED / FILLED / CANCEL_REJECTED (48 bytes)
    MarketUpdate.h   ADD / MODIFY / CANCEL / TRADE, anonymous (32 bytes)
    Order.h          resting order node of the order book (64 bytes)
tests/         test runner and tests for everything above
```

## Design notes

- **No allocation on the hot path.** Objects that come and go while trading (orders, price levels) live in
  memory pools sized at startup; buffers and queues are allocated once.
- **Threads talk through lock-free queues.** Each component (gateway, matching engine, publisher) runs on its
  own pinned thread. The matching engine is single-threaded, so the order book needs no locks and results are
  deterministic.
- **Nothing blocks.** Sockets are non-blocking and polled from a busy loop. A full TCP send buffer is kept and
  flushed later; a multicast message the OS can't take is dropped (receivers detect gaps by sequence number).
- **Fixed-size wire messages.** Copied with `memcpy`, native little-endian, fields ordered largest-first,
  explicit zeroed padding so every byte is defined (`static_assert`ed with
  `std::has_unique_object_representations_v`).
- **Prices are integers** (ticks), never floating point.
- **Logging is cheap for the caller.** `log()` stores the format string pointer and up to four values; formatting
  and file I/O happen on the logger thread.

## Measurements

Ryzen 9 5900X, Windows 10, Release build, loopback. The machine was not isolated, so tails are noisy;
treat these as orders of magnitude.

| What | Result |
|---|---|
| `new` + `delete` vs memory pool allocate + deallocate | ~37-41 ns vs ~2 ns |
| Thread-to-thread one-way latency: mutex-protected queue vs `LFQueue` | ~3,100 ns vs ~80 ns |
| Formatting and writing a log line vs `Logger::log()` (incl. two clock reads) | ~3,200 ns vs ~71 ns |
| TCP one-way latency (ping-pong / 2) | ~2.7 us |
| UDP unicast one-way latency | ~3.4 us |
| Multicast send over Windows loopback | ~20-37 us per send |
| Empty non-blocking `recv` poll | ~310 ns |

Two Windows details found along the way: sending multicast on the loopback interface fails with
`WSAENETUNREACH` until some local socket has joined the group, and a socket that isn't read drops the newest
datagrams once its 64 KB receive buffer is full.

## Building

Requirements: Visual Studio 2026 (MSVC toolset v145) with the C++ desktop workload, or CMake 3.20+ with MSVC.

Visual Studio: open `LowLatency.slnx`, pick `Debug` or `Release` / `x64`, build. The test executable is written
to `build\<Configuration>\LowLatencyTests.exe`.

CMake (from a Developer Command Prompt):

```
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
build\cmake\LowLatencyTests.exe
```

## Tests

`LowLatencyTests` runs every test and prints one line per test; the exit code is non-zero if any check failed.
Pass part of a name to run a subset:

```
LowLatencyTests tcp
LowLatencyTests queue
```

The network tests use `127.0.0.1` only, and the multicast tests use groups `239.1.1.x` on the loopback interface,
so nothing leaves the machine. Allow the executable through the Windows firewall if it asks.

## Roadmap

1. Order book: price levels, add/cancel, best bid/ask.
2. Matching engine: price-time priority, responses and market updates.
3. Order gateway (TCP) and market data publisher (multicast with sequence numbers, snapshots).
4. Exchange executable wiring the three threads together.
5. Trading client: market data consumer, order gateway client, order book copy, features, risk checks,
   position keeping, market-making and liquidity-taking strategies.
6. Benchmarks with latency histograms, profiling and tuning.
7. Recording and replay for backtesting.
