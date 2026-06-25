#include "iq/Lex/Lexer.h"

#include "iq/Diag/Diagnostic.h"
#include "iq/Source/SourceManager.h"

#include <format>

namespace iq
{

Lexer::Lexer(
    SourceManager const& sm,
    StringInterner& interner,
    DiagnosticEngine& diags
)
    : m_sm(sm), m_src(sm.text()), m_interner(interner), m_diags(diags)
{
}

char Lexer::peek(std::uint32_t ahead) const
{
    std::uint32_t const idx = m_pos + ahead;
    return idx < m_src.size() ? m_src[idx] : '\0';
}

bool Lexer::match(char expected)
{
    if (atEnd() || m_src[m_pos] != expected)
    {
        return false;
    }
    ++m_pos;
    return true;
}

Token Lexer::make(TokenKind kind, SourceOffset start) const
{
    return Token{ kind, SourceSpan{ start, m_pos }, StringInterner::kInvalid };
}

void Lexer::skipTrivia()
{
    for (;;)
    {
        while (!atEnd() && charInfo::has(peek(), charInfo::Whitespace))
        {
            advance();
        }

        if (peek() == '/' && peek(1) == '/')
        {
            while (!atEnd() && peek() != '\n')
            {
                advance();
            }
            continue;
        }

        if (peek() == '/' && peek(1) == '*')
        {
            SourceOffset const start = m_pos;
            advance();
            advance();
            while (!atEnd() && !(peek() == '*' && peek(1) == '/'))
            {
                advance();
            }
            if (atEnd())
            {
                m_diags.error(SourceSpan{ start, m_pos }, "unterminated block comment");
                return;
            }
            advance();
            advance();
            continue;
        }

        break;
    }
}

Token Lexer::lexIdentifierOrKeyword(SourceOffset start)
{
    while (!atEnd() && charInfo::has(peek(), charInfo::IdentCont))
    {
        advance();
    }

    std::string_view const text = m_src.substr(start, m_pos - start);
    if (text == "_")
    {
        return make(TokenKind::Underscore, start);
    }
    if (auto const kw = keywordKind(text))
    {
        return make(*kw, start);
    }

    Token tok = make(TokenKind::Identifier, start);
    tok.symbol = m_interner.intern(text);
    return tok;
}

Token Lexer::lexNumber(SourceOffset start)
{
    while (!atEnd() && charInfo::has(peek(), charInfo::Digit))
    {
        advance();
    }

    if (peek() == '.' && charInfo::has(peek(1), charInfo::Digit))
    {
        advance();
        while (!atEnd() && charInfo::has(peek(), charInfo::Digit))
        {
            advance();
        }
    }

    if (charInfo::has(peek(), charInfo::IdentStart))
    {
        while (!atEnd() && charInfo::has(peek(), charInfo::IdentCont))
        {
            advance();
        }
    }

    return make(TokenKind::Number, start);
}

Token Lexer::lexString(SourceOffset start)
{
    advance();

    while (!atEnd() && peek() != '"')
    {
        if (peek() == '\n')
        {
            break;
        }
        if (peek() == '\\')
        {
            advance();
            if (!atEnd())
            {
                advance();
            }
        }
        else
        {
            advance();
        }
    }

    if (atEnd() || peek() != '"')
    {
        m_diags.error(SourceSpan{ start, m_pos }, "unterminated string literal");
        return make(TokenKind::Error, start);
    }

    advance();
    return make(TokenKind::String, start);
}

Token Lexer::next()
{
    skipTrivia();

    SourceOffset const start = m_pos;
    if (atEnd())
    {
        return make(TokenKind::Eof, start);
    }

    char const c = peek();

    if (charInfo::has(c, charInfo::IdentStart))
    {
        return lexIdentifierOrKeyword(start);
    }
    if (charInfo::has(c, charInfo::Digit))
    {
        return lexNumber(start);
    }
    if (c == '"')
    {
        return lexString(start);
    }

    advance();
    switch (c)
    {
    case '(': return make(TokenKind::LParen, start);
    case ')': return make(TokenKind::RParen, start);
    case '{': return make(TokenKind::LBrace, start);
    case '}': return make(TokenKind::RBrace, start);
    case '[': return make(TokenKind::LBracket, start);
    case ']': return make(TokenKind::RBracket, start);
    case ',': return make(TokenKind::Comma, start);
    case ';': return make(TokenKind::Semicolon, start);
    case ':': return make(TokenKind::Colon, start);
    case '^': return make(TokenKind::Caret, start);

    case '+': return make(match('=') ? TokenKind::PlusEq : TokenKind::Plus, start);
    case '*': return make(match('=') ? TokenKind::StarEq : TokenKind::Star, start);
    case '/': return make(match('=') ? TokenKind::SlashEq : TokenKind::Slash, start);
    case '%': return make(match('=') ? TokenKind::PercentEq : TokenKind::Percent, start);
    case '=': return make(match('=') ? TokenKind::EqEq : TokenKind::Eq, start);
    case '!': return make(match('=') ? TokenKind::BangEq : TokenKind::Bang, start);
    case '<': return make(match('=') ? TokenKind::LtEq : TokenKind::Lt, start);
    case '>': return make(match('=') ? TokenKind::GtEq : TokenKind::Gt, start);

    case '-':
        if (match('>'))
        {
            return make(TokenKind::Arrow, start);
        }
        return make(match('=') ? TokenKind::MinusEq : TokenKind::Minus, start);

    case '.':
        if (match('.'))
        {
            return make(match('=') ? TokenKind::DotDotEq : TokenKind::DotDot, start);
        }
        return make(TokenKind::Dot, start);

    case '&':
        if (match('&'))
        {
            return make(TokenKind::AmpAmp, start);
        }
        break;

    case '|':
        if (match('|'))
        {
            return make(TokenKind::PipePipe, start);
        }
        break;
    }

    m_diags.error(SourceSpan{ start, m_pos },
                  std::format("unexpected character '{}'", c));
    return make(TokenKind::Error, start);
}

std::vector<Token> Lexer::tokenize()
{
    std::vector<Token> tokens;
    for (;;)
    {
        Token tok = next();
        tokens.push_back(tok);
        if (tok.kind == TokenKind::Eof)
        {
            break;
        }
    }
    return tokens;
}

} // namespace iq