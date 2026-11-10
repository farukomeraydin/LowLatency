#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include "Assert.h"

namespace Common {

    // A fixed-capacity pool of objects of type T. All memory is allocated once, up front;
    // after that, getting and returning objects never calls new/delete.
    // Not thread-safe: each pool is meant to be used by a single thread.
    template <typename T>
    class MemoryPool {
    public:
        // Allocates room for `capacity` objects up front and links every block into the free list:
        // 0 -> 1 -> 2 -> ... -> capacity-1 -> NO_BLOCK.
        explicit MemoryPool(std::size_t capacity)
            : blocks_(capacity) {
            if (capacity == 0) {
                fatal("MemoryPool: capacity must be greater than 0");
            }
            for (std::size_t i = 0; i < capacity; ++i) {
                blocks_[i].nextFree = (i + 1 < capacity) ? i + 1 : NO_BLOCK;
            }
            firstFree_ = 0;
        }

        // A pool hands out pointers into its own blocks. A copy would be a different pool those pointers
        // do not belong to, and a moved-from pool would be left with no blocks but a stale free list.
        // So pools can be neither copied nor moved.
        MemoryPool(const MemoryPool&) = delete;
        MemoryPool& operator=(const MemoryPool&) = delete;
        MemoryPool(MemoryPool&&) = delete;
        MemoryPool& operator=(MemoryPool&&) = delete;

        // Runs the destructor of every object that is still allocated (never given back with deallocate()).
        // Objects whose destructor does nothing (e.g. plain numbers) need no work at all.
        ~MemoryPool() {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                // The free blocks are exactly those reachable through the free list; all others hold an object.
                std::vector<bool> isFree(blocks_.size(), false);
                for (std::size_t i = firstFree_; i != NO_BLOCK; i = blocks_[i].nextFree) {
                    isFree[i] = true;
                }
                for (std::size_t i = 0; i < blocks_.size(); ++i) {
                    if (!isFree[i]) {
                        std::launder(reinterpret_cast<T*>(blocks_[i].storage))->~T();
                    }
                }
            }
        }

        // Takes the first free block and constructs a T in it, passing args to T's constructor.
        // Stops the program if every block is in use. If T's constructor throws, the pool is unchanged.
        template <typename... Args>
        [[nodiscard]] T* allocate(Args&&... args) {
            if (firstFree_ == NO_BLOCK) [[unlikely]] {
                fatal("MemoryPool: no free blocks left");
            }
            const std::size_t index = firstFree_;
            T* object = ::new (static_cast<void*>(blocks_[index].storage)) T(std::forward<Args>(args)...);
            firstFree_ = blocks_[index].nextFree;   // only after the object was built successfully
            return object;
        }

        // Destroys the object and gives its block back to the pool.
        // object must be a pointer returned by this pool's allocate() that was not deallocated yet.
        void deallocate(T* object) noexcept {
            // Find the block from the address: each block is sizeof(Block) bytes, starting at blocks_.data(),
            // and the object lives at the very start of its block (storage is the first member).
            const auto objectAddress = reinterpret_cast<std::uintptr_t>(object);
            const auto firstAddress  = reinterpret_cast<std::uintptr_t>(blocks_.data());
            assertTrue(objectAddress >= firstAddress, "MemoryPool::deallocate: pointer is not from this pool");
            const std::size_t offset = objectAddress - firstAddress;
            const std::size_t index  = offset / sizeof(Block);
            assertTrue(index < blocks_.size() && offset % sizeof(Block) == 0,
                       "MemoryPool::deallocate: pointer is not from this pool");

            object->~T();                        // end the object's lifetime (no memory is freed)
            blocks_[index].nextFree = firstFree_; // push the block onto the front of the free list
            firstFree_ = index;
        }

    private:
        // One slot of the pool: raw memory, big enough and correctly aligned for one T,
        // plus the index of the next free block (meaningful only while this block is free).
        struct Block {
            alignas(T) std::byte storage[sizeof(T)];
            std::size_t nextFree;
        };

        // "No block" marker for the free list (same sentinel idea as the *_INVALID constants).
        static constexpr std::size_t NO_BLOCK = std::numeric_limits<std::size_t>::max();

        std::vector<Block> blocks_;          // every slot of the pool, allocated once
        std::size_t firstFree_ = NO_BLOCK;   // index of the first free block (head of the free list)
    };

}
