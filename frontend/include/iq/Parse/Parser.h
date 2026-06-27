#pragma once

#include "iq/AST/AST.h"
#include "iq/AST/Arena.h"
#include "iq/Lex/Token.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace iq
{

class SourceManager;
class StringInterner;
class DiagnosticEngine;

// Recursive-descent parser for declarations and statements, with a Pratt
// (precedence-climbing) core for expressions. Nodes are allocated in the arena.
//
// Error strategy: a failed parse returns nullptr after emitting a diagnostic;
// the caller resynchronizes (panic-mode recovery) to a statement boundary and
// keeps going, so one error does not abort the whole file. (We deliberately use
// nullptr sentinels rather than std::expected here -- recovery wants to keep
// parsing past an error, which the sentinel + sync-token model expresses more
// directly than threading expected<> through every production.)
class Parser
{
public:
    Parser(
        std::vector<Token> tokens,
        SourceManager const& sm,
        StringInterner& interner,
        DiagnosticEngine& diags,
        Arena& arena
    );

    Module parseModule();

private:
    // --- token cursor -----------------------------------------------------
    Token const& current() const;
    Token const& previous() const;
    Token const& peek(std::uint32_t ahead) const;
    bool atEnd() const;
    bool check(TokenKind kind) const;
    Token const& advance();
    bool match(TokenKind kind);
    bool expect(
        TokenKind kind,
        std::string_view what
    );

    // --- diagnostics / recovery ------------------------------------------
    void errorAtCurrent(std::string message);
    void synchronize();

    SourceSpan spanFrom(SourceOffset start) const;
    SourceOffset prevEnd() const;

    // --- declarations -----------------------------------------------------
    Decl* parseDecl();
    FnDecl* parseFn();
    ConstDecl* parseConst();

    // --- statements -------------------------------------------------------
    Stmt* parseStmt();
    BlockStmt* parseBlock();
    Stmt* parseLet(bool isConst);
    Stmt* parseReturn();
    Stmt* parseIf();
    Stmt* parseWhile();
    Stmt* parseFor();

    // --- expressions (Pratt) ---------------------------------------------
    Expr* parseExpr(int minBp = 0);
    Expr* parsePrefix();
    Expr* parseParenOrTuple(SourceOffset start);
    Expr* parseArrayLiteral(SourceOffset start);
    Expr* parseNumber(Token const& tok);
    Expr* parseString(Token const& tok);
    Expr* parseCallTail(Expr* callee);
    Expr* parseIndexTail(Expr* base);
    Expr* parseFieldTail(Expr* base);

    // --- types & patterns -------------------------------------------------
    TypeExpr* parseType();
    Pattern* parsePattern();

    std::vector<Token> m_tokens;
    SourceManager const& m_sm;
    StringInterner& m_interner;
    DiagnosticEngine& m_diags;
    Arena& m_arena;
    std::size_t m_pos = 0;
};

} // namespace iq