#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace iq
{

class StringInterner
{
public:
    using Symbol = std::uint32_t;
    static constexpr Symbol kInvalid = 0;

    StringInterner()
    {
        m_storage.emplace_back();
    }

    Symbol intern(std::string_view text)
    {
        if (auto it = m_lookup.find(text); it != m_lookup.end())
        {
            return it->second;
        }

        Symbol const id = static_cast<Symbol>(m_storage.size());
        std::string const& stored = m_storage.emplace_back(text);
        m_lookup.emplace(std::string_view{ stored }, id);
        return id;
    }

    std::string_view lookup(Symbol id) const
    {
        return m_storage[id];
    }

private:
    std::deque<std::string> m_storage;
    std::unordered_map<std::string_view, Symbol> m_lookup;
};

} // namespace iq