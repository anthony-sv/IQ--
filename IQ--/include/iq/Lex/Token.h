#pragma once

#include "iq/Lex/TokenKind.h"
#include "iq/Source/SourceLocation.h"
#include "iq/Support/StringInterner.h"

namespace iq
{

struct Token
{
    TokenKind kind = TokenKind::Eof;
    SourceSpan span;
    StringInterner::Symbol symbol = StringInterner::kInvalid;

    bool is(TokenKind k) const
    {
        return kind == k;
    }
};

} // namespace iq