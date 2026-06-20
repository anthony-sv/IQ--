#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace iq
{

// A bump (arena) allocator. Allocations are carved sequentially out of large
// chunks; nothing is ever freed individually. The whole arena is released at
// once when it is destroyed.
//
// Two consequences shape the rest of the AST:
//  * Pointers handed out stay valid for the arena's lifetime. We allocate in a
//    list of fixed chunks (never one growing buffer) so a later allocation can
//    never relocate an earlier node.
//  * Destructors are NEVER run. Every type placed here must be trivially
//    destructible -- enforced by a static_assert in make()/makeArray(). That is
//    the whole point: AST nodes hold interned symbols and arena-allocated spans,
//    never owning std::string / std::vector members.
class Arena
{
public:
    Arena() = default;

    Arena(Arena const&) = delete;
    Arena& operator=(Arena const&) = delete;
    Arena(Arena&&) noexcept = default;
    Arena& operator=(Arena&&) noexcept = default;
    ~Arena() = default;

    // Raw, aligned allocation. Prefer make()/makeArray() for typed nodes.
    void* allocate(
        std::size_t bytes,
        std::size_t align
    );

    // Construct a single T in the arena and return a pointer to it.
    template <typename T, typename... Args>
    T* make(Args&&... args)
    {
        static_assert(
            std::is_trivially_destructible_v<T>,
            "Arena never runs destructors; nodes must be trivially destructible"
        );
        void* const mem = allocate(sizeof(T), alignof(T));
        return std::construct_at(static_cast<T*>(mem), std::forward<Args>(args)...);
    }

    // Allocate an uninitialized array of count Ts.
    template <typename T>
    std::span<T> makeArrayUninit(std::size_t count)
    {
        static_assert(
            std::is_trivially_destructible_v<T>,
            "Arena never runs destructors; elements must be trivially destructible"
        );
        if (count == 0)
        {
            return {};
        }
        void* const mem = allocate(sizeof(T) * count, alignof(T));
        return std::span<T>{ static_cast<T*>(mem), count };
    }

    // Copy a contiguous range into a fresh arena array. Used by the parser to
    // freeze a std::vector of children into a stable arena span.
    template <typename T>
    std::span<T> makeArray(std::span<T const> src)
    {
        std::span<T> const out = makeArrayUninit<T>(src.size());
        std::uninitialized_copy(src.begin(), src.end(), out.begin());
        return out;
    }

    std::size_t bytesUsed() const
    {
        return m_bytesUsed;
    }

private:
    struct Chunk
    {
        std::unique_ptr<std::byte[]> data;
        std::size_t size = 0;
    };

    static constexpr std::size_t kFirstChunkSize = 4 * 1024;
    static constexpr std::size_t kMaxChunkSize = 256 * 1024;

    void grow(std::size_t minBytes);

    std::vector<Chunk> m_chunks;
    std::byte* m_cur = nullptr;
    std::byte* m_end = nullptr;
    std::size_t m_nextChunkSize = kFirstChunkSize;
    std::size_t m_bytesUsed = 0;
};

} // namespace iq