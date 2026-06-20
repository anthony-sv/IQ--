#pragma once

#include "iq/Lex/Token.h"
#include "iq/Source/SourceLocation.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace iq
{

class SourceManager;
class DiagnosticEngine;

namespace charInfo
{
enum Flags : std::uint8_t
{
    Whitespace = 1 << 0,
    Digit = 1 << 1,
    IdentStart = 1 << 2,
    IdentCont = 1 << 3,
};

constexpr std::array<std::uint8_t, 256> buildTable()
{
    std::array<std::uint8_t, 256> t{};
    auto set = [&t](unsigned char c, std::uint8_t f) { t[c] |= f; };

    for (unsigned char c : { ' ', '\t', '\r', '\n', '\f', '\v' })
    {
        set(c, Whitespace);
    }
    for (char c = '0'; c <= '9'; ++c)
    {
        set(static_cast<unsigned char>(c), Digit | IdentCont);
    }
    for (char c = 'a'; c <= 'z'; ++c)
    {
        set(static_cast<unsigned char>(c), IdentStart | IdentCont);
    }
    for (char c = 'A'; c <= 'Z'; ++c)
    {
        set(static_cast<unsigned char>(c), IdentStart | IdentCont);
    }
    set('_', IdentStart | IdentCont);
    return t;
}

inline constexpr std::array<std::uint8_t, 256> kTable = buildTable();

constexpr bool has(char c, Flags f)
{
    return (kTable[static_cast<unsigned char>(c)] & f) != 0;
}
} // namespace charInfo

class Lexer
{
public:
    Lexer(
        SourceManager const& sm,
        StringInterner& interner,
        DiagnosticEngine& diags
    );

    Token next();

    std::vector<Token> tokenize();

private:
    char peek(std::uint32_t ahead = 0) const;

    bool atEnd() const
    {
        return m_pos >= m_src.size();
    }

    char advance()
    {
        return m_src[m_pos++];
    }

    bool match(char expected);

    void skipTrivia();

    Token make(TokenKind kind, SourceOffset start) const;
    Token lexIdentifierOrKeyword(SourceOffset start);
    Token lexNumber(SourceOffset start);
    Token lexString(SourceOffset start);

    SourceManager const& m_sm;
    std::string_view m_src;
    StringInterner& m_interner;
    DiagnosticEngine& m_diags;
    SourceOffset m_pos = 0;
};

} // namespace iq