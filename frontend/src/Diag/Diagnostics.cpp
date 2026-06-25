#include "iq/Diag/Diagnostic.h"
#include "iq/Source/SourceManager.h"

#include <print>
#include <string>

namespace iq
{

namespace
{

std::string_view severityLabel(Severity s)
{
    switch (s)
    {
    case Severity::Error:   return "error";
    case Severity::Warning: return "warning";
    case Severity::Note:    return "note";
    }
    return "diagnostic";
}

} // namespace

void DiagnosticEngine::report(
    Severity severity,
    SourceSpan span,
    std::string message
)
{
    if (severity == Severity::Error)
    {
        ++m_errorCount;
    }
    m_diagnostics.push_back(Diagnostic{ severity, span, std::move(message) });
}

void DiagnosticEngine::print(Diagnostic const& diag) const
{
    LineCol const lc = m_sm.lineCol(diag.span.begin);

    std::println(stderr, "{}:{}:{}: {}: {}",
                 m_sm.filename(), lc.line, lc.col,
                 severityLabel(diag.severity), diag.message);

    std::string_view const line = m_sm.lineText(lc.line);
    std::println(stderr, "    {}", line);

    std::string caret(lc.col - 1, ' ');
    caret.push_back('^');
    std::uint32_t const spanLen = diag.span.length();
    for (std::uint32_t i = 1; i < spanLen && (lc.col - 1 + i) < line.size(); ++i)
    {
        caret.push_back('~');
    }

    std::println(stderr, "    {}", caret);
}

void DiagnosticEngine::printAll() const
{
    for (Diagnostic const& diag : m_diagnostics)
    {
        print(diag);
    }
}

} // namespace iq