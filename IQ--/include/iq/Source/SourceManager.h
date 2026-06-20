#pragma once

#include "iq/Source/SourceLocation.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iq
{

class SourceManager
{
public:
    SourceManager(std::string filename, std::string source);

    static std::optional<SourceManager> fromFile(std::filesystem::path const& path);

    std::string_view filename() const
    {
        return m_filename;
    }

    std::string_view text() const
    {
        return m_source;
    }

    SourceOffset size() const
    {
        return static_cast<SourceOffset>(m_source.size());
    }

    LineCol lineCol(SourceOffset offset) const;

    std::string_view lineText(std::uint32_t line) const;

    std::uint32_t lineCount() const
    {
        return static_cast<std::uint32_t>(m_lineStarts.size());
    }

    std::string_view spanText(SourceSpan span) const
    {
        return std::string_view{ m_source }.substr(span.begin, span.length());
    }

private:
    void computeLineStarts();

    std::string m_filename;
    std::string m_source;
    std::vector<SourceOffset> m_lineStarts;
};

} // namespace iq