#include <catch2/catch_test_macros.hpp>

#include "iq/Diag/Diagnostic.h"
#include "iq/Lex/Lexer.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using iq::TokenKind;

namespace
{

// Lex a snippet down to just its token-kind sequence (always ends in Eof).
std::vector<TokenKind> lexKinds(std::string_view src)
{
    iq::SourceManager sm("<test>", std::string(src));
    iq::StringInterner interner;
    iq::DiagnosticEngine diags(sm);
    iq::Lexer lexer(sm, interner, diags);

    std::vector<TokenKind> kinds;
    for (iq::Token const& tok : lexer.tokenize())
    {
        kinds.push_back(tok.kind);
    }
    return kinds;
}

std::size_t lexErrorCount(std::string_view src)
{
    iq::SourceManager sm("<test>", std::string(src));
    iq::StringInterner interner;
    iq::DiagnosticEngine diags(sm);
    iq::Lexer lexer(sm, interner, diags);
    lexer.tokenize();
    return diags.errorCount();
}

} // namespace

TEST_CASE("lexer: keywords vs identifiers", "[lexer]")
{
    REQUIRE(lexKinds("fn let const x") == std::vector<TokenKind>{
        TokenKind::KwFn, TokenKind::KwLet, TokenKind::KwConst,
        TokenKind::Identifier, TokenKind::Eof
    });
}

TEST_CASE("lexer: multi-character operators", "[lexer]")
{
    REQUIRE(lexKinds("-> == != <= >= += ..=") == std::vector<TokenKind>{
        TokenKind::Arrow, TokenKind::EqEq, TokenKind::BangEq,
        TokenKind::LtEq, TokenKind::GtEq, TokenKind::PlusEq,
        TokenKind::DotDotEq, TokenKind::Eof
    });
}

TEST_CASE("lexer: numbers and string literal", "[lexer]")
{
    REQUIRE(lexKinds("42 3.5 42i64 \"hi\"") == std::vector<TokenKind>{
        TokenKind::Number, TokenKind::Number, TokenKind::Number,
        TokenKind::String, TokenKind::Eof
    });
}

TEST_CASE("lexer: comments are trivia", "[lexer]")
{
    REQUIRE(lexKinds("a // line\n b /* block */ c") == std::vector<TokenKind>{
        TokenKind::Identifier, TokenKind::Identifier, TokenKind::Identifier,
        TokenKind::Eof
    });
}

TEST_CASE("lexer: unterminated string reports an error", "[lexer]")
{
    REQUIRE(lexErrorCount("\"oops") == 1);
    REQUIRE(lexErrorCount("\"ok\"") == 0);
}

TEST_CASE("lexer: unterminated block comment reports an error", "[lexer]")
{
    REQUIRE(lexErrorCount("/* never closed") == 1);
}
