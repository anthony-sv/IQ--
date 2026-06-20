#include "iq/Source/SourceManager.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace iq
{

SourceManager::SourceManager(std::string filename, std::string source)
    : m_filename(std::move(filename)), m_source(std::move(source))
{
    computeLineStarts();
}

std::optional<SourceManager> SourceManager::fromFile(std::filesystem::path const& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }

    std::string contents
    {
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    };
    return SourceManager{ path.string(), std::move(contents) };
}

void SourceManager::computeLineStarts()
{
    m_lineStarts.clear();
    m_lineStarts.push_back(0);
    for (SourceOffset i = 0; i < m_source.size(); ++i)
    {
        if (m_source[i] == '\n')
        {
            m_lineStarts.push_back(i + 1);
        }
    }
}

LineCol SourceManager::lineCol(SourceOffset offset) const
{
    auto const it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), offset);
    auto const lineIndex = static_cast<std::uint32_t>(std::distance(m_lineStarts.begin(), it) - 1);
    SourceOffset const lineStart = m_lineStarts[lineIndex];
    return LineCol{ lineIndex + 1, offset - lineStart + 1 };
}

std::string_view SourceManager::lineText(std::uint32_t line) const
{
    if (line == 0 || line > m_lineStarts.size())
    {
        return {};
    }

    SourceOffset start = m_lineStarts[line - 1];
    SourceOffset end = (line < m_lineStarts.size()) ? m_lineStarts[line] : size();

    if (end > start && m_source[end - 1] == '\n')
    {
        --end;
    }
    if (end > start && m_source[end - 1] == '\r')
    {
        --end;
    }

    return std::string_view{ m_source }.substr(start, end - start);
}

} // namespace iq