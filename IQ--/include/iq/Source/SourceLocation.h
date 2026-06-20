#pragma once

#include <cstdint>

namespace iq
{

using SourceOffset = std::uint32_t;

struct SourceSpan
{
    SourceOffset begin = 0;
    SourceOffset end = 0;

    constexpr std::uint32_t length() const
    {
        return end - begin;
    }

    constexpr bool empty() const
    {
        return begin == end;
    }

    static constexpr SourceSpan merge(SourceSpan const a, SourceSpan const b)
    {
        return SourceSpan
        {
            a.begin < b.begin ? a.begin : b.begin,
            a.end > b.end ? a.end : b.end
        };
    }
};

struct LineCol
{
    std::uint32_t line = 1;
    std::uint32_t col = 1;
};

} // namespace iq