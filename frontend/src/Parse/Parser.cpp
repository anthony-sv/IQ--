#include "iq/Parse/Parser.h"

#include "iq/Diag/Diagnostic.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <format>
#include <optional>
#include <utility>

namespace iq
{

namespace
{

// Binding powers for the Pratt loop. Higher binds tighter. Left/right pairs
// encode associativity: left < right is left-associative; left > right is
// right-associative (used by assignment).
struct InfixBp
{
    int left;
    int right;
};

// Postfix and cast precedences live above every binary operator.
constexpr int kCallBp = 20;
constexpr int kIndexBp = 20;
constexpr int kCastBp = 18;
// Prefix unary operators parse their operand at this power: tighter than `as`
// (18) so that `-x as i64` groups as `(-x) as i64`.
constexpr int kUnaryBp = 19;

std::optional<BinaryOp> toBinaryOp(TokenKind k)
{
    switch (k)
    {
    case TokenKind::Plus:     return BinaryOp::Add;
    case TokenKind::Minus:    return BinaryOp::Sub;
    case TokenKind::Star:     return BinaryOp::Mul;
    case TokenKind::Slash:    return BinaryOp::Div;
    case TokenKind::Percent:  return BinaryOp::Rem;
    case TokenKind::EqEq:     return BinaryOp::Eq;
    case TokenKind::BangEq:   return BinaryOp::Ne;
    case TokenKind::Lt:       return BinaryOp::Lt;
    case TokenKind::LtEq:     return BinaryOp::Le;
    case TokenKind::Gt:       return BinaryOp::Gt;
    case TokenKind::GtEq:     return BinaryOp::Ge;
    case TokenKind::AmpAmp:   return BinaryOp::And;
    case TokenKind::PipePipe: return BinaryOp::Or;
    default:                  return std::nullopt;
    }
}

InfixBp binaryBp(BinaryOp op)
{
    switch (op)
    {
    case BinaryOp::Mul:
    case BinaryOp::Div:
    case BinaryOp::Rem: return { 16, 17 };
    case BinaryOp::Add:
    case BinaryOp::Sub: return { 14, 15 };
    case BinaryOp::Lt:
    case BinaryOp::Le:
    case BinaryOp::Gt:
    case BinaryOp::Ge:  return { 12, 13 };
    case BinaryOp::Eq:
    case BinaryOp::Ne:  return { 10, 11 };
    case BinaryOp::And: return { 8, 9 };
    case BinaryOp::Or:  return { 6, 7 };
    }
    return { 0, 0 };
}

std::optional<AssignOp> toAssignOp(TokenKind k)
{
    switch (k)
    {
    case TokenKind::Eq:        return AssignOp::Assign;
    case TokenKind::PlusEq:    return AssignOp::Add;
    case TokenKind::MinusEq:   return AssignOp::Sub;
    case TokenKind::StarEq:    return AssignOp::Mul;
    case TokenKind::SlashEq:   return AssignOp::Div;
    case TokenKind::PercentEq: return AssignOp::Rem;
    default:                   return std::nullopt;
    }
}

std::optional<NumSuffix> parseNumSuffix(std::string_view s)
{
    if (s.empty())  { return NumSuffix::None; }
    if (s == "i32") { return NumSuffix::I32; }
    if (s == "i64") { return NumSuffix::I64; }
    if (s == "u32") { return NumSuffix::U32; }
    if (s == "u64") { return NumSuffix::U64; }
    if (s == "f32") { return NumSuffix::F32; }
    if (s == "f64") { return NumSuffix::F64; }
    return std::nullopt;
}

} // namespace

Parser::Parser(
    std::vector<Token> tokens,
    SourceManager const& sm,
    StringInterner& interner,
    DiagnosticEngine& diags,
    Arena& arena
)
    : m_tokens(std::move(tokens))
    , m_sm(sm)
    , m_interner(interner)
    , m_diags(diags)
    , m_arena(arena)
{
}

// ---------------------------------------------------------------------------
// Token cursor
// ---------------------------------------------------------------------------

Token const& Parser::current() const
{
    return m_tokens[m_pos];
}

Token const& Parser::previous() const
{
    return m_tokens[m_pos == 0 ? 0 : m_pos - 1];
}

Token const& Parser::peek(std::uint32_t ahead) const
{
    std::size_t const idx = m_pos + ahead;
    return idx < m_tokens.size() ? m_tokens[idx] : m_tokens.back();
}

bool Parser::atEnd() const
{
    return current().kind == TokenKind::Eof;
}

bool Parser::check(TokenKind kind) const
{
    return current().kind == kind;
}

Token const& Parser::advance()
{
    Token const& tok = current();
    if (m_pos + 1 < m_tokens.size())
    {
        ++m_pos;
    }
    return tok;
}

bool Parser::match(TokenKind kind)
{
    if (check(kind))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(
    TokenKind kind,
    std::string_view what
)
{
    if (check(kind))
    {
        advance();
        return true;
    }
    errorAtCurrent(std::format(
        "expected {}, found '{}'",
        what,
        tokenKindName(current().kind)
    ));
    return false;
}

// ---------------------------------------------------------------------------
// Diagnostics / recovery
// ---------------------------------------------------------------------------

void Parser::errorAtCurrent(std::string message)
{
    m_diags.error(current().span, std::move(message));
}

void Parser::synchronize()
{
    advance();      // discard the token that triggered the error
    while (!atEnd())
    {
        if (previous().kind == TokenKind::Semicolon)
        {
            return;
        }
        switch (current().kind)
        {
        case TokenKind::KwFn:
        case TokenKind::KwLet:
        case TokenKind::KwConst:
        case TokenKind::KwIf:
        case TokenKind::KwWhile:
        case TokenKind::KwFor:
        case TokenKind::KwRet:
        case TokenKind::RBrace:
            return;
        default:
            advance();
        }
    }
}

SourceSpan Parser::spanFrom(SourceOffset start) const
{
    return SourceSpan{ start, prevEnd() };
}

SourceOffset Parser::prevEnd() const
{
    return previous().span.end;
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

Module Parser::parseModule()
{
    std::vector<Decl*> decls;
    while (!atEnd())
    {
        if (Decl* d = parseDecl())
        {
            decls.push_back(d);
        }
        else
        {
            synchronize();
        }
    }
    return Module{ m_arena.makeArray<Decl*>(std::span<Decl* const>(decls)) };
}

Decl* Parser::parseDecl()
{
    switch (current().kind)
    {
    case TokenKind::KwFn:    return parseFn();
    case TokenKind::KwConst: return parseConst();
    default:
        errorAtCurrent(std::format(
            "expected a declaration ('fn' or 'const'), found '{}'",
            tokenKindName(current().kind)
        ));
        return nullptr;
    }
}

FnDecl* Parser::parseFn()
{
    SourceOffset const start = current().span.begin;
    advance();      // 'fn'

    if (!check(TokenKind::Identifier))
    {
        errorAtCurrent("expected a function name after 'fn'");
        return nullptr;
    }
    Symbol const name = current().symbol;
    advance();

    if (!expect(TokenKind::LParen, "'(' after function name"))
    {
        return nullptr;
    }

    std::vector<Param> params;
    while (!check(TokenKind::RParen) && !atEnd())
    {
        SourceOffset const pStart = current().span.begin;
        if (!check(TokenKind::Identifier))
        {
            errorAtCurrent("expected a parameter name");
            return nullptr;
        }
        Symbol const pName = current().symbol;
        advance();

        if (!expect(TokenKind::Colon, "':' after parameter name"))
        {
            return nullptr;
        }
        TypeExpr* const pType = parseType();
        if (!pType)
        {
            return nullptr;
        }
        params.push_back(Param{ pName, pType, spanFrom(pStart) });

        if (!match(TokenKind::Comma))
        {
            break;
        }
    }

    if (!expect(TokenKind::RParen, "')' after parameters"))
    {
        return nullptr;
    }

    TypeExpr* returnType = nullptr;
    if (match(TokenKind::Arrow))
    {
        returnType = parseType();
        if (!returnType)
        {
            return nullptr;
        }
    }

    BlockStmt* const body = parseBlock();
    if (!body)
    {
        return nullptr;
    }

    return m_arena.make<FnDecl>(
        spanFrom(start),
        name,
        m_arena.makeArray<Param>(std::span<Param const>(params)),
        returnType,
        body
    );
}

ConstDecl* Parser::parseConst()
{
    SourceOffset const start = current().span.begin;
    advance();      // 'const'

    if (!check(TokenKind::Identifier))
    {
        errorAtCurrent("expected a name after 'const'");
        return nullptr;
    }
    Symbol const name = current().symbol;
    advance();

    TypeExpr* type = nullptr;
    if (match(TokenKind::Colon))
    {
        type = parseType();
        if (!type)
        {
            return nullptr;
        }
    }

    if (!expect(TokenKind::Eq, "'=' in const declaration"))
    {
        return nullptr;
    }
    Expr* const init = parseExpr();
    if (!init)
    {
        return nullptr;
    }
    expect(TokenKind::Semicolon, "';' after const declaration");

    return m_arena.make<ConstDecl>(spanFrom(start), name, type, init);
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

Stmt* Parser::parseStmt()
{
    switch (current().kind)
    {
    case TokenKind::KwLet:   return parseLet(false);
    case TokenKind::KwConst: return parseLet(true);
    case TokenKind::KwRet:   return parseReturn();
    case TokenKind::KwIf:    return parseIf();
    case TokenKind::KwWhile: return parseWhile();
    case TokenKind::KwFor:   return parseFor();
    case TokenKind::LBrace:  return parseBlock();

    case TokenKind::KwBreak:
    {
        SourceOffset const start = current().span.begin;
        advance();
        expect(TokenKind::Semicolon, "';' after 'break'");
        return m_arena.make<BreakStmt>(spanFrom(start));
    }
    case TokenKind::KwContinue:
    {
        SourceOffset const start = current().span.begin;
        advance();
        expect(TokenKind::Semicolon, "';' after 'continue'");
        return m_arena.make<ContinueStmt>(spanFrom(start));
    }

    default:
    {
        SourceOffset const start = current().span.begin;
        Expr* const e = parseExpr();
        if (!e)
        {
            return nullptr;
        }
        expect(TokenKind::Semicolon, "';' after expression statement");
        return m_arena.make<ExprStmt>(spanFrom(start), e);
    }
    }
}

BlockStmt* Parser::parseBlock()
{
    SourceOffset const start = current().span.begin;
    if (!expect(TokenKind::LBrace, "'{'"))
    {
        return nullptr;
    }

    std::vector<Stmt*> stmts;
    while (!check(TokenKind::RBrace) && !atEnd())
    {
        if (Stmt* s = parseStmt())
        {
            stmts.push_back(s);
        }
        else
        {
            synchronize();
        }
    }

    expect(TokenKind::RBrace, "'}' to close block");
    return m_arena.make<BlockStmt>(
        spanFrom(start),
        m_arena.makeArray<Stmt*>(std::span<Stmt* const>(stmts))
    );
}

Stmt* Parser::parseLet(bool isConst)
{
    SourceOffset const start = current().span.begin;
    advance();      // 'let' / 'const'

    Pattern* const pattern = parsePattern();
    if (!pattern)
    {
        return nullptr;
    }

    TypeExpr* type = nullptr;
    if (match(TokenKind::Colon))
    {
        type = parseType();
        if (!type)
        {
            return nullptr;
        }
    }

    Expr* init = nullptr;
    if (match(TokenKind::Eq))
    {
        init = parseExpr();
        if (!init)
        {
            return nullptr;
        }
    }

    expect(TokenKind::Semicolon, "';' after binding");
    return m_arena.make<LetStmt>(spanFrom(start), pattern, type, init, isConst);
}

Stmt* Parser::parseReturn()
{
    SourceOffset const start = current().span.begin;
    advance();      // 'ret'

    Expr* value = nullptr;
    if (!check(TokenKind::Semicolon))
    {
        value = parseExpr();
        if (!value)
        {
            return nullptr;
        }
    }
    expect(TokenKind::Semicolon, "';' after 'ret'");
    return m_arena.make<ReturnStmt>(spanFrom(start), value);
}

Stmt* Parser::parseIf()
{
    SourceOffset const start = current().span.begin;
    advance();      // 'if'

    Expr* const cond = parseExpr();
    if (!cond)
    {
        return nullptr;
    }
    BlockStmt* const thenBranch = parseBlock();
    if (!thenBranch)
    {
        return nullptr;
    }

    Stmt* elseBranch = nullptr;
    if (match(TokenKind::KwElse))
    {
        elseBranch = check(TokenKind::KwIf) ? parseIf() : parseBlock();
        if (!elseBranch)
        {
            return nullptr;
        }
    }

    return m_arena.make<IfStmt>(spanFrom(start), cond, thenBranch, elseBranch);
}

Stmt* Parser::parseWhile()
{
    SourceOffset const start = current().span.begin;
    advance();      // 'while'

    Expr* const cond = parseExpr();
    if (!cond)
    {
        return nullptr;
    }
    BlockStmt* const body = parseBlock();
    if (!body)
    {
        return nullptr;
    }
    return m_arena.make<WhileStmt>(spanFrom(start), cond, body);
}

Stmt* Parser::parseFor()
{
    SourceOffset const start = current().span.begin;
    advance();      // 'for'

    Pattern* const var = parsePattern();
    if (!var)
    {
        return nullptr;
    }
    if (!expect(TokenKind::KwIn, "'in' after for-loop variable"))
    {
        return nullptr;
    }
    Expr* const iter = parseExpr();
    if (!iter)
    {
        return nullptr;
    }
    BlockStmt* const body = parseBlock();
    if (!body)
    {
        return nullptr;
    }
    return m_arena.make<ForStmt>(spanFrom(start), var, iter, body);
}

// ---------------------------------------------------------------------------
// Expressions (Pratt)
// ---------------------------------------------------------------------------

Expr* Parser::parseExpr(int minBp)
{
    Expr* lhs = parsePrefix();
    if (!lhs)
    {
        return nullptr;
    }

    for (;;)
    {
        TokenKind const k = current().kind;

        // Postfix: call a(...)
        if (k == TokenKind::LParen && kCallBp >= minBp)
        {
            lhs = parseCallTail(lhs);
            if (!lhs)
            {
                return nullptr;
            }
            continue;
        }

        // Postfix: index a[i] / a[^k]
        if (k == TokenKind::LBracket && kIndexBp >= minBp)
        {
            lhs = parseIndexTail(lhs);
            if (!lhs)
            {
                return nullptr;
            }
            continue;
        }

        // Cast: value as Type
        if (k == TokenKind::KwAs && kCastBp >= minBp)
        {
            advance();
            TypeExpr* const type = parseType();
            if (!type)
            {
                return nullptr;
            }
            lhs = m_arena.make<CastExpr>(
                SourceSpan{ lhs->span.begin, type->span.end },
                lhs,
                type
            );
            continue;
        }

        // Range: lo..hi / lo..=hi
        if (k == TokenKind::DotDot || k == TokenKind::DotDotEq)
        {
            constexpr InfixBp kRangeBp{ 4, 5 };
            if (kRangeBp.left < minBp)
            {
                break;
            }
            bool const inclusive = (k == TokenKind::DotDotEq);
            advance();
            Expr* const rhs = parseExpr(kRangeBp.right);
            if (!rhs)
            {
                return nullptr;
            }
            lhs = m_arena.make<RangeExpr>(
                SourceSpan{ lhs->span.begin, rhs->span.end },
                lhs,
                rhs,
                inclusive
            );
            continue;
        }

        // Assignment (right-associative): target = value, target += value, ...
        if (std::optional<AssignOp> const assign = toAssignOp(k))
        {
            constexpr InfixBp kAssignBp{ 2, 1 };
            if (kAssignBp.left < minBp)
            {
                break;
            }
            advance();
            Expr* const rhs = parseExpr(kAssignBp.right);
            if (!rhs)
            {
                return nullptr;
            }
            lhs = m_arena.make<AssignExpr>(
                SourceSpan{ lhs->span.begin, rhs->span.end },
                *assign,
                lhs,
                rhs
            );
            continue;
        }

        // Binary operators.
        if (std::optional<BinaryOp> const op = toBinaryOp(k))
        {
            InfixBp const bp = binaryBp(*op);
            if (bp.left < minBp)
            {
                break;
            }
            advance();
            Expr* const rhs = parseExpr(bp.right);
            if (!rhs)
            {
                return nullptr;
            }
            lhs = m_arena.make<BinaryExpr>(
                SourceSpan{ lhs->span.begin, rhs->span.end },
                *op,
                lhs,
                rhs
            );
            continue;
        }

        break;
    }

    return lhs;
}

Expr* Parser::parsePrefix()
{
    Token const tok = current();
    SourceOffset const start = tok.span.begin;

    switch (tok.kind)
    {
    case TokenKind::Number:
        advance();
        return parseNumber(tok);
    case TokenKind::String:
        advance();
        return parseString(tok);
    case TokenKind::KwTrue:
        advance();
        return m_arena.make<BoolLiteral>(tok.span, true);
    case TokenKind::KwFalse:
        advance();
        return m_arena.make<BoolLiteral>(tok.span, false);
    case TokenKind::Identifier:
        advance();
        return m_arena.make<NameExpr>(tok.span, tok.symbol);

    case TokenKind::Minus:
    {
        advance();
        Expr* const operand = parseExpr(kUnaryBp);
        if (!operand)
        {
            return nullptr;
        }
        return m_arena.make<UnaryExpr>(spanFrom(start), UnaryOp::Neg, operand);
    }
    case TokenKind::Bang:
    {
        advance();
        Expr* const operand = parseExpr(kUnaryBp);
        if (!operand)
        {
            return nullptr;
        }
        return m_arena.make<UnaryExpr>(spanFrom(start), UnaryOp::Not, operand);
    }

    case TokenKind::LParen:
        advance();
        return parseParenOrTuple(start);
    case TokenKind::LBracket:
        advance();
        return parseArrayLiteral(start);

    default:
        errorAtCurrent(std::format(
            "expected an expression, found '{}'",
            tokenKindName(tok.kind)
        ));
        return nullptr;
    }
}

Expr* Parser::parseParenOrTuple(SourceOffset start)
{
    // '(' already consumed. '()' is the unit tuple.
    if (match(TokenKind::RParen))
    {
        return m_arena.make<TupleExpr>(spanFrom(start), std::span<Expr*>{});
    }

    Expr* const first = parseExpr();
    if (!first)
    {
        return nullptr;
    }

    // A bare '(expr)' is just grouping; a comma promotes it to a tuple.
    if (!check(TokenKind::Comma))
    {
        expect(TokenKind::RParen, "')' to close parenthesized expression");
        return first;
    }

    std::vector<Expr*> elems;
    elems.push_back(first);
    while (match(TokenKind::Comma))
    {
        if (check(TokenKind::RParen))
        {
            break;      // trailing comma, e.g. (x,)
        }
        Expr* const e = parseExpr();
        if (!e)
        {
            return nullptr;
        }
        elems.push_back(e);
    }

    expect(TokenKind::RParen, "')' to close tuple");
    return m_arena.make<TupleExpr>(
        spanFrom(start),
        m_arena.makeArray<Expr*>(std::span<Expr* const>(elems))
    );
}

Expr* Parser::parseArrayLiteral(SourceOffset start)
{
    // '[' already consumed.
    std::vector<Expr*> elems;
    while (!check(TokenKind::RBracket) && !atEnd())
    {
        Expr* const e = parseExpr();
        if (!e)
        {
            return nullptr;
        }
        elems.push_back(e);
        if (!match(TokenKind::Comma))
        {
            break;
        }
    }
    expect(TokenKind::RBracket, "']' to close array literal");
    return m_arena.make<ArrayExpr>(
        spanFrom(start),
        m_arena.makeArray<Expr*>(std::span<Expr* const>(elems))
    );
}

Expr* Parser::parseNumber(Token const& tok)
{
    std::string_view const text = m_sm.spanText(tok.span);

    // Split the literal into its numeric part and an optional type suffix.
    std::size_t split = 0;
    while (split < text.size()
        && ((text[split] >= '0' && text[split] <= '9') || text[split] == '.'))
    {
        ++split;
    }
    std::string_view const digits = text.substr(0, split);
    std::string_view const suffixText = text.substr(split);

    std::optional<NumSuffix> const suffix = parseNumSuffix(suffixText);
    if (!suffix)
    {
        errorAtCurrent(std::format("unknown numeric suffix '{}'", suffixText));
        return m_arena.make<NumberLiteral>(tok.span, false, NumSuffix::None);
    }

    bool const isFloat = digits.find('.') != std::string_view::npos
        || *suffix == NumSuffix::F32
        || *suffix == NumSuffix::F64;

    return m_arena.make<NumberLiteral>(tok.span, isFloat, *suffix);
}

Expr* Parser::parseString(Token const& tok)
{
    // The token span includes the surrounding quotes; intern the inner text.
    // Escape sequences are stored verbatim for now (decoding is a later step).
    std::string_view text = m_sm.spanText(tok.span);
    if (text.size() >= 2)
    {
        text = text.substr(1, text.size() - 2);
    }
    Symbol const value = m_interner.intern(text);
    return m_arena.make<StringLiteral>(tok.span, value);
}

Expr* Parser::parseCallTail(Expr* callee)
{
    advance();      // '('
    std::vector<Expr*> args;
    while (!check(TokenKind::RParen) && !atEnd())
    {
        Expr* const arg = parseExpr();
        if (!arg)
        {
            return nullptr;
        }
        args.push_back(arg);
        if (!match(TokenKind::Comma))
        {
            break;
        }
    }
    expect(TokenKind::RParen, "')' to close argument list");
    return m_arena.make<CallExpr>(
        SourceSpan{ callee->span.begin, prevEnd() },
        callee,
        m_arena.makeArray<Expr*>(std::span<Expr* const>(args))
    );
}

Expr* Parser::parseIndexTail(Expr* base)
{
    advance();      // '['
    bool const fromEnd = match(TokenKind::Caret);
    Expr* const index = parseExpr();
    if (!index)
    {
        return nullptr;
    }
    expect(TokenKind::RBracket, "']' to close index");
    return m_arena.make<IndexExpr>(
        SourceSpan{ base->span.begin, prevEnd() },
        base,
        index,
        fromEnd
    );
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

TypeExpr* Parser::parseType()
{
    SourceOffset const start = current().span.begin;

    switch (current().kind)
    {
    case TokenKind::Identifier:
    {
        Symbol const name = current().symbol;
        advance();
        return m_arena.make<NamedType>(spanFrom(start), name);
    }

    case TokenKind::LBracket:
    {
        advance();      // '['
        TypeExpr* const element = parseType();
        if (!element)
        {
            return nullptr;
        }
        if (!expect(TokenKind::Semicolon, "';' in array type '[T; N]'"))
        {
            return nullptr;
        }
        Expr* const size = parseExpr();
        if (!size)
        {
            return nullptr;
        }
        expect(TokenKind::RBracket, "']' to close array type");
        return m_arena.make<ArrayType>(spanFrom(start), element, size);
    }

    case TokenKind::LParen:
    {
        advance();      // '('
        if (match(TokenKind::RParen))
        {
            return m_arena.make<TupleType>(spanFrom(start), std::span<TypeExpr*>{});
        }

        std::vector<TypeExpr*> types;
        bool trailingComma = false;
        for (;;)
        {
            TypeExpr* const t = parseType();
            if (!t)
            {
                return nullptr;
            }
            types.push_back(t);
            if (match(TokenKind::Comma))
            {
                if (check(TokenKind::RParen))
                {
                    trailingComma = true;
                    break;
                }
                continue;
            }
            break;
        }
        expect(TokenKind::RParen, "')' to close tuple type");

        // '(T)' without a comma is just grouping; otherwise it is a tuple.
        if (types.size() == 1 && !trailingComma)
        {
            return types.front();
        }
        return m_arena.make<TupleType>(
            spanFrom(start),
            m_arena.makeArray<TypeExpr*>(std::span<TypeExpr* const>(types))
        );
    }

    default:
        errorAtCurrent(std::format(
            "expected a type, found '{}'",
            tokenKindName(current().kind)
        ));
        return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

Pattern* Parser::parsePattern()
{
    SourceOffset const start = current().span.begin;

    switch (current().kind)
    {
    case TokenKind::Underscore:
        advance();
        return m_arena.make<WildcardPattern>(spanFrom(start));

    case TokenKind::Identifier:
    {
        Symbol const name = current().symbol;
        advance();
        return m_arena.make<IdentPattern>(spanFrom(start), name);
    }

    // Rest pattern: '..' ignores, '..name' binds the gap (array patterns only;
    // tuple-context validity is enforced later in sema).
    case TokenKind::DotDot:
    {
        advance();
        Symbol name = StringInterner::kInvalid;
        if (check(TokenKind::Identifier))
        {
            name = current().symbol;
            advance();
        }
        return m_arena.make<RestPattern>(spanFrom(start), name);
    }

    case TokenKind::LParen:
    {
        advance();
        std::vector<Pattern*> elems;
        while (!check(TokenKind::RParen) && !atEnd())
        {
            Pattern* const p = parsePattern();
            if (!p)
            {
                return nullptr;
            }
            elems.push_back(p);
            if (!match(TokenKind::Comma))
            {
                break;
            }
        }
        expect(TokenKind::RParen, "')' to close tuple pattern");
        return m_arena.make<TuplePattern>(
            spanFrom(start),
            m_arena.makeArray<Pattern*>(std::span<Pattern* const>(elems))
        );
    }

    case TokenKind::LBracket:
    {
        advance();
        std::vector<Pattern*> elems;
        while (!check(TokenKind::RBracket) && !atEnd())
        {
            Pattern* const p = parsePattern();
            if (!p)
            {
                return nullptr;
            }
            elems.push_back(p);
            if (!match(TokenKind::Comma))
            {
                break;
            }
        }
        expect(TokenKind::RBracket, "']' to close array pattern");
        return m_arena.make<ArrayPattern>(
            spanFrom(start),
            m_arena.makeArray<Pattern*>(std::span<Pattern* const>(elems))
        );
    }

    default:
        errorAtCurrent(std::format(
            "expected a pattern, found '{}'",
            tokenKindName(current().kind)
        ));
        return nullptr;
    }
}

} // namespace iq