#include "iq/AST/Arena.h"

namespace iq
{

namespace
{

// Round addr up to the next multiple of align (align is a power of two).
std::uintptr_t alignUp(
    std::uintptr_t addr,
    std::size_t align
)
{
    std::uintptr_t const mask = static_cast<std::uintptr_t>(align) - 1;
    return (addr + mask) & ~mask;
}

} // namespace

void* Arena::allocate(
    std::size_t bytes,
    std::size_t align
)
{
    std::uintptr_t aligned = alignUp(reinterpret_cast<std::uintptr_t>(m_cur), align);
    std::byte* p = reinterpret_cast<std::byte*>(aligned);

    if (p + bytes > m_end)
    {
        grow(bytes + align);
        aligned = alignUp(reinterpret_cast<std::uintptr_t>(m_cur), align);
        p = reinterpret_cast<std::byte*>(aligned);
    }

    m_cur = p + bytes;
    m_bytesUsed += bytes;
    return p;
}

void Arena::grow(std::size_t minBytes)
{
    std::size_t size = m_nextChunkSize;
    if (size < minBytes)
    {
        size = minBytes;
    }

    auto data = std::make_unique<std::byte[]>(size);
    m_cur = data.get();
    m_end = data.get() + size;
    m_chunks.push_back(Chunk{ std::move(data), size });

    if (m_nextChunkSize < kMaxChunkSize)
    {
        m_nextChunkSize *= 2;
    }
}

} // namespace iq