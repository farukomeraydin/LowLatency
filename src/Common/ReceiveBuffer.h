#pragma once

#include <cstddef>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include "Assert.h"
#include "Network.h"

namespace Common {

    // Collects the bytes arriving from a TCP byte stream, so that WHOLE messages can be cut out of them
    // even when one receive returns only part of a message, or several messages glued together.
    //
    // Layout of storage_:
    //   [ already consumed | readable: received, not yet consumed | writable: free space ]
    //   0               readPos_                              writePos_            capacity()
    class ReceiveBuffer {
    public:
        // Allocates all memory up front (at startup), never while trading.
        explicit ReceiveBuffer(std::size_t capacity) : storage_(capacity) {
            if (capacity == 0) {
                fatal("ReceiveBuffer: capacity must be greater than 0");
            }
        }

        // Copying would duplicate the buffer by accident (and cost an allocation): not allowed.
        ReceiveBuffer(const ReceiveBuffer&) = delete;
        ReceiveBuffer& operator=(const ReceiveBuffer&) = delete;

        // Moving hands the memory over; the moved-from buffer is left empty (capacity 0, nothing readable).
        ReceiveBuffer(ReceiveBuffer&& other) noexcept
            : storage_(std::move(other.storage_)),
              readPos_(std::exchange(other.readPos_, 0)),
              writePos_(std::exchange(other.writePos_, 0)) {}

        ReceiveBuffer& operator=(ReceiveBuffer&& other) noexcept {
            if (this != &other) {
                storage_ = std::move(other.storage_);
                readPos_ = std::exchange(other.readPos_, 0);
                writePos_ = std::exchange(other.writePos_, 0);
            }
            return *this;
        }

        [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }

        // The bytes received but not yet consumed (a view into the buffer: valid until the next change).
        [[nodiscard]] std::span<const std::byte> readable() const noexcept {
            return std::span{storage_}.subspan(readPos_, writePos_ - readPos_);
        }

        // How many more bytes fit at the end.
        [[nodiscard]] std::size_t freeSpace() const noexcept { return storage_.size() - writePos_; }

        // Receives whatever has arrived on the socket into the free space at the end, and adds it to the
        // readable bytes. Returns tryReceive's result (OK + bytes, WOULD_BLOCK, CLOSED, ERR).
        // With no free space left it returns ERR without receiving: the caller must consume first.
        [[nodiscard]] IoResult receiveFrom(Socket& socket) noexcept {
            if (freeSpace() < capacity() / 2) {
                compact();   // little room left at the end: move the leftover bytes to the front first
            }
            if (freeSpace() == 0) [[unlikely]] {
                // Never call recv with 0 bytes: it would return 0, which looks exactly like "connection closed".
                return {IoStatus::ERR, 0};
            }
            const IoResult result = tryReceive(socket, std::span{storage_}.subspan(writePos_));
            if (result.status == IoStatus::OK) {
                writePos_ += result.bytes;
            }
            return result;
        }

        // Removes the first `count` readable bytes (the messages the caller has finished handling).
        // When nothing readable is left, both positions go back to 0, so the whole buffer is free again.
        void consume(std::size_t count) noexcept {
            assertTrue(count <= writePos_ - readPos_, "ReceiveBuffer::consume: more bytes than are readable");
            readPos_ += count;
            if (readPos_ == writePos_) {
                readPos_ = 0;
                writePos_ = 0;
            }
        }

    private:
        // Moves the readable bytes (usually the start of one partial message) to the front of the buffer,
        // so that all consumed space becomes free space at the end again.
        void compact() noexcept {
            if (readPos_ == 0) {
                return;   // already at the front: nothing to win
            }
            // readPos_ > 0 means at least one byte is readable: consume() resets both positions to 0
            // as soon as everything is consumed.
            const std::size_t leftover = writePos_ - readPos_;
            std::memmove(storage_.data(), storage_.data() + readPos_, leftover);   // the ranges may overlap
            readPos_ = 0;
            writePos_ = leftover;
        }

        std::vector<std::byte> storage_;
        std::size_t readPos_ = 0;    // first byte not yet consumed
        std::size_t writePos_ = 0;   // where the next received byte goes
    };

}
