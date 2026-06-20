#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace iq
{

enum class TokenKind
{
    Eof,
    Error,

    Identifier,
    Number,
    String,

    KwFn,
    KwLet,
    KwConst,
    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwIn,
    KwRet,
    KwBreak,
    KwContinue,
    KwAs,
    KwTrue,
    KwFalse,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Colon,
    Arrow,
    Dot,
    DotDot,
    DotDotEq,
    Caret,
    Underscore,

    Eq,
    EqEq,
    BangEq,
    Lt,
    LtEq,
    Gt,
    GtEq,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    PlusEq,
    MinusEq,
    StarEq,
    SlashEq,
    PercentEq,
    Bang,
    AmpAmp,
    PipePipe,
};

constexpr std::string_view tokenKindName(TokenKind kind)
{
    switch (kind)
    {
    case TokenKind::Eof:        return "Eof";
    case TokenKind::Error:      return "Error";
    case TokenKind::Identifier: return "Identifier";
    case TokenKind::Number:     return "Number";
    case TokenKind::String:     return "String";
    case TokenKind::KwFn:       return "fn";
    case TokenKind::KwLet:      return "let";
    case TokenKind::KwConst:    return "const";
    case TokenKind::KwIf:       return "if";
    case TokenKind::KwElse:     return "else";
    case TokenKind::KwWhile:    return "while";
    case TokenKind::KwFor:      return "for";
    case TokenKind::KwIn:       return "in";
    case TokenKind::KwRet:      return "ret";
    case TokenKind::KwBreak:    return "break";
    case TokenKind::KwContinue: return "continue";
    case TokenKind::KwAs:       return "as";
    case TokenKind::KwTrue:     return "true";
    case TokenKind::KwFalse:    return "false";
    case TokenKind::LParen:     return "LParen";
    case TokenKind::RParen:     return "RParen";
    case TokenKind::LBrace:     return "LBrace";
    case TokenKind::RBrace:     return "RBrace";
    case TokenKind::LBracket:   return "LBracket";
    case TokenKind::RBracket:   return "RBracket";
    case TokenKind::Comma:      return "Comma";
    case TokenKind::Semicolon:  return "Semicolon";
    case TokenKind::Colon:      return "Colon";
    case TokenKind::Arrow:      return "Arrow";
    case TokenKind::Dot:        return "Dot";
    case TokenKind::DotDot:     return "DotDot";
    case TokenKind::DotDotEq:   return "DotDotEq";
    case TokenKind::Caret:      return "Caret";
    case TokenKind::Underscore: return "Underscore";
    case TokenKind::Eq:         return "Eq";
    case TokenKind::EqEq:       return "EqEq";
    case TokenKind::BangEq:     return "BangEq";
    case TokenKind::Lt:         return "Lt";
    case TokenKind::LtEq:       return "LtEq";
    case TokenKind::Gt:         return "Gt";
    case TokenKind::GtEq:       return "GtEq";
    case TokenKind::Plus:       return "Plus";
    case TokenKind::Minus:      return "Minus";
    case TokenKind::Star:       return "Star";
    case TokenKind::Slash:      return "Slash";
    case TokenKind::Percent:    return "Percent";
    case TokenKind::PlusEq:     return "PlusEq";
    case TokenKind::MinusEq:    return "MinusEq";
    case TokenKind::StarEq:     return "StarEq";
    case TokenKind::SlashEq:    return "SlashEq";
    case TokenKind::PercentEq:  return "PercentEq";
    case TokenKind::Bang:       return "Bang";
    case TokenKind::AmpAmp:     return "AmpAmp";
    case TokenKind::PipePipe:   return "PipePipe";
    }
    return "<?>";
}

constexpr std::optional<TokenKind> keywordKind(std::string_view text)
{
    struct Entry
    {
        std::string_view spelling;
        TokenKind kind;
    };

    constexpr std::array<Entry, 14> kKeywords
    {
        {
            { "fn", TokenKind::KwFn },
            { "let", TokenKind::KwLet },
            { "const", TokenKind::KwConst },
            { "if", TokenKind::KwIf },
            { "else", TokenKind::KwElse },
            { "while", TokenKind::KwWhile },
            { "for", TokenKind::KwFor },
            { "in", TokenKind::KwIn },
            { "ret", TokenKind::KwRet },
            { "break", TokenKind::KwBreak },
            { "continue", TokenKind::KwContinue },
            { "as", TokenKind::KwAs },
            { "true", TokenKind::KwTrue },
            { "false", TokenKind::KwFalse },
        }
    };

    for (Entry const& e : kKeywords)
    {
        if (e.spelling == text)
            return e.kind;
    }
    return std::nullopt;
}

} // namespace iq