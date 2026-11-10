#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "Assert.h"
#include "Types.h"

namespace Common {

    // A lock-free queue for exactly ONE producer thread and ONE consumer thread (SPSC),
    // built on a fixed-size ring buffer that is allocated once, in the constructor.
    template <typename T>
    class LFQueue {
    public:
        // capacity must be a power of two (1, 2, 4, 8, ...), so that "position % capacity"
        // can be computed as "position & mask_", which is much cheaper than a division.
        explicit LFQueue(std::size_t capacity)
            : storage_(capacity), mask_(capacity - 1) {
            if (!std::has_single_bit(capacity)) {
                fatal("LFQueue: capacity must be a power of two");
            }
        }

        // The producer and consumer threads hold references to one queue; copying or moving it makes no sense.
        LFQueue(const LFQueue&) = delete;
        LFQueue& operator=(const LFQueue&) = delete;
        LFQueue(LFQueue&&) = delete;
        LFQueue& operator=(LFQueue&&) = delete;

        // PRODUCER THREAD ONLY. Copies value into the next free slot and publishes it to the consumer.
        // Returns false, and changes nothing, if the queue is full.
        [[nodiscard]] bool tryPush(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>) {
            const std::size_t write = writeIndex_.load(std::memory_order_relaxed);   // our own index
            const std::size_t read  = readIndex_.load(std::memory_order_acquire);    // the consumer's progress
            if (write - read == storage_.size()) [[unlikely]] {
                return false;   // full: every slot holds an element the consumer has not taken yet
            }
            storage_[write & mask_] = value;                          // 1) write the element
            writeIndex_.store(write + 1, std::memory_order_release);  // 2) then publish it
            return true;
        }

        // CONSUMER THREAD ONLY. Copies the oldest element into out and gives its slot back to the producer.
        // Returns false, and leaves out unchanged, if the queue is empty.
        [[nodiscard]] bool tryPop(T& out) noexcept(std::is_nothrow_copy_assignable_v<T>) {
            const std::size_t read  = readIndex_.load(std::memory_order_relaxed);    // our own index
            const std::size_t write = writeIndex_.load(std::memory_order_acquire);   // the producer's progress
            if (read == write) {
                return false;   // empty: nothing has been published that we have not taken yet
            }
            out = storage_[read & mask_];                            // 1) copy the element out
            readIndex_.store(read + 1, std::memory_order_release);   // 2) then free the slot
            return true;
        }

    private:
        std::vector<T> storage_;   // the ring buffer slots; never resized after construction
        std::size_t mask_;         // capacity - 1, e.g. 1023 = 0b1111111111 for capacity 1024

        // How many elements have ever been pushed. Written only by the producer.
        // Own cache line, so the consumer's writes to readIndex_ never cause false sharing with it.
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> writeIndex_{0};

        // How many elements have ever been popped. Written only by the consumer.
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> readIndex_{0};
    };

}
