#pragma once

#include <cstddef>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include "Assert.h"
#include "Network.h"

namespace Common {

    // Holds outgoing bytes that the OS could not take yet (its send buffer was full), so the caller never
    // has to wait and never sends half a message by accident. Same layout as ReceiveBuffer:
    //   [ already sent | pending: appended, not yet sent | free space ]
    //   0           readPos_                         writePos_     capacity()
    class SendBuffer {
    public:
        // Allocates all memory up front (at startup), never while trading.
        explicit SendBuffer(std::size_t capacity) : storage_(capacity) {
            if (capacity == 0) {
                fatal("SendBuffer: capacity must be greater than 0");
            }
        }

        SendBuffer(const SendBuffer&) = delete;
        SendBuffer& operator=(const SendBuffer&) = delete;

        // Moving hands the memory over; the moved-from buffer is left empty (capacity 0, nothing pending).
        SendBuffer(SendBuffer&& other) noexcept
            : storage_(std::move(other.storage_)),
              readPos_(std::exchange(other.readPos_, 0)),
              writePos_(std::exchange(other.writePos_, 0)) {}

        SendBuffer& operator=(SendBuffer&& other) noexcept {
            if (this != &other) {
                storage_ = std::move(other.storage_);
                readPos_ = std::exchange(other.readPos_, 0);
                writePos_ = std::exchange(other.writePos_, 0);
            }
            return *this;
        }

        [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }

        // How many more bytes fit at the end.
        [[nodiscard]] std::size_t freeSpace() const noexcept { return storage_.size() - writePos_; }

        // The bytes appended but not yet sent.
        [[nodiscard]] std::span<const std::byte> pending() const noexcept {
            return std::span{storage_}.subspan(readPos_, writePos_ - readPos_);
        }

        // Copies `data` (normally one whole message) to the end of the pending bytes.
        // All or nothing: if it does not fit completely, nothing is added and it returns false,
        // so a message is never cut in half.
        [[nodiscard]] bool append(std::span<const std::byte> data) noexcept {
            if (data.size() > freeSpace()) {
                compact();   // maybe the already-sent space at the front gives enough room
            }
            if (data.size() > freeSpace()) [[unlikely]] {
                return false;
            }
            if (!data.empty()) {
                std::memcpy(storage_.data() + writePos_, data.data(), data.size());
                writePos_ += data.size();
            }
            return true;
        }

        // Sends as many pending bytes as the OS takes right now, and removes them from the buffer.
        // Never waits. Returns trySend's result; with nothing pending it returns {OK, 0} without a system call.
        [[nodiscard]] IoResult sendTo(Socket& socket) noexcept {
            if (readPos_ == writePos_) {
                return {IoStatus::OK, 0};   // the usual case: nothing to send, so skip the ~300 ns system call
            }
            const IoResult result = trySend(socket, pending());
            if (result.status == IoStatus::OK) {
                readPos_ += result.bytes;
                if (readPos_ == writePos_) {
                    readPos_ = 0;   // everything sent: the whole buffer is free again
                    writePos_ = 0;
                }
            }
            return result;
        }

    private:
        // Moves the pending bytes to the front, so that the already-sent space becomes free space at the end.
        void compact() noexcept {
            if (readPos_ == 0) {
                return;   // already at the front: nothing to win
            }
            const std::size_t leftover = writePos_ - readPos_;
            std::memmove(storage_.data(), storage_.data() + readPos_, leftover);   // the ranges may overlap
            readPos_ = 0;
            writePos_ = leftover;
        }

        std::vector<std::byte> storage_;
        std::size_t readPos_ = 0;    // first byte not yet sent
        std::size_t writePos_ = 0;   // where the next appended byte goes
    };

}
