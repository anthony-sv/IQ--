#pragma once

#include "iq/Source/SourceLocation.h"

#include <string>
#include <vector>

namespace iq
{

class SourceManager;

enum class Severity
{
    Error,
    Warning,
    Note,
};

struct Diagnostic
{
    Severity severity = Severity::Error;
    SourceSpan span;
    std::string message;
};

class DiagnosticEngine
{
public:
    explicit DiagnosticEngine(SourceManager const& sm)
        : m_sm(sm)
    {
    }

    void report(
        Severity severity,
        SourceSpan span,
        std::string message
    );

    void error(SourceSpan span, std::string message)
    {
        report(Severity::Error, span, std::move(message));
    }

    void warning(SourceSpan span, std::string message)
    {
        report(Severity::Warning, span, std::move(message));
    }

    bool hasErrors() const
    {
        return m_errorCount > 0;
    }

    std::size_t errorCount() const
    {
        return m_errorCount;
    }

    std::vector<Diagnostic> const& all() const
    {
        return m_diagnostics;
    }

    void printAll() const;

private:
    void print(Diagnostic const& diag) const;

    SourceManager const& m_sm;
    std::vector<Diagnostic> m_diagnostics;
    std::size_t m_errorCount = 0;
};

} // namespace iq