#pragma once

#include "iq/Source/SourceLocation.h"
#include "iq/Support/StringInterner.h"

#include <cstdint>
#include <span>
#include <string_view>

// The IQ-- abstract syntax tree.
//
// Every node is allocated in an Arena and is trivially destructible: it stores
// interned symbols (Symbol), value enums, source spans, child pointers, and
// arena-allocated spans of child pointers -- never an owning std::string or
// std::vector. Nodes are mutable on purpose; later passes (name resolution,
// type checking) annotate them in place.
//
// Five independent node categories, each with its own base + kind enum:
//     TypeExpr   type annotations         (i32, [T; N], (A, B))
//     Pattern    binding patterns         (x, _, (a, b), [a, .., b])
//     Expr       expressions
//     Stmt       statements
//     Decl       top-level declarations
//
// RTTI is the LLVM-style tag-and-classof idiom; see Casting.h for isa<>/cast<>.

namespace iq
{

using Symbol = StringInterner::Symbol;

// Resolved binding, filled in by name resolution. Forward-declared so the AST
// only ever holds a pointer to it (it lives in iq/Sema/Sym.h).
struct Sym;

// Resolved semantic type, filled in by the type checker (iq/Sema/Type.h).
struct Type;

// ---------------------------------------------------------------------------
// Operators (decoupled from TokenKind so the AST owns its own vocabulary).
// ---------------------------------------------------------------------------

enum class BinaryOp : std::uint8_t
{
    Add, Sub, Mul, Div, Rem,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or,
};

enum class UnaryOp : std::uint8_t
{
    Neg,    // -x
    Not,    // !x
};

enum class AssignOp : std::uint8_t
{
    Assign,     // =
    Add,        // +=
    Sub,        // -=
    Mul,        // *=
    Div,        // /=
    Rem,        // %=
};

// A numeric literal records only its optional suffix; whether it is finally an
// i32 / i64 / f64 / ... is a sema concern (bidirectional type checking), never
// decided here. None means "let an expected type or the default decide".
enum class NumSuffix : std::uint8_t
{
    None,
    I32, I64, U32, U64, F32, F64,
};

constexpr std::string_view binaryOpName(BinaryOp op)
{
    switch (op)
    {
    case BinaryOp::Add: return "+";
    case BinaryOp::Sub: return "-";
    case BinaryOp::Mul: return "*";
    case BinaryOp::Div: return "/";
    case BinaryOp::Rem: return "%";
    case BinaryOp::Eq:  return "==";
    case BinaryOp::Ne:  return "!=";
    case BinaryOp::Lt:  return "<";
    case BinaryOp::Le:  return "<=";
    case BinaryOp::Gt:  return ">";
    case BinaryOp::Ge:  return ">=";
    case BinaryOp::And: return "&&";
    case BinaryOp::Or:  return "||";
    }
    return "<?>";
}

constexpr std::string_view unaryOpName(UnaryOp op)
{
    switch (op)
    {
    case UnaryOp::Neg: return "-";
    case UnaryOp::Not: return "!";
    }
    return "<?>";
}

constexpr std::string_view assignOpName(AssignOp op)
{
    switch (op)
    {
    case AssignOp::Assign: return "=";
    case AssignOp::Add:    return "+=";
    case AssignOp::Sub:    return "-=";
    case AssignOp::Mul:    return "*=";
    case AssignOp::Div:    return "/=";
    case AssignOp::Rem:    return "%=";
    }
    return "<?>";
}

// ---------------------------------------------------------------------------
// Type expressions.
// ---------------------------------------------------------------------------

enum class TypeKind : std::uint8_t
{
    Named,      // i32, string, void, user-named
    Array,      // [T; N]
    Tuple,      // (A, B); () is the unit/void type
};

struct TypeExpr
{
    TypeKind kind;
    SourceSpan span;

protected:
    constexpr TypeExpr(TypeKind k, SourceSpan s)
        : kind(k), span(s)
    {
    }
};

struct NamedType : TypeExpr
{
    static constexpr TypeKind Kind = TypeKind::Named;
    Symbol name;

    constexpr NamedType(SourceSpan s, Symbol n)
        : TypeExpr(Kind, s), name(n)
    {
    }

    static bool classof(TypeExpr const* t)
    {
        return t->kind == Kind;
    }
};

struct ArrayType : TypeExpr
{
    static constexpr TypeKind Kind = TypeKind::Array;
    TypeExpr* element;
    struct Expr* size;      // length expression, e.g. the N in [T; N]

    constexpr ArrayType(SourceSpan s, TypeExpr* elem, struct Expr* len)
        : TypeExpr(Kind, s), element(elem), size(len)
    {
    }

    static bool classof(TypeExpr const* t)
    {
        return t->kind == Kind;
    }
};

struct TupleType : TypeExpr
{
    static constexpr TypeKind Kind = TypeKind::Tuple;
    std::span<TypeExpr*> elements;

    constexpr TupleType(SourceSpan s, std::span<TypeExpr*> elems)
        : TypeExpr(Kind, s), elements(elems)
    {
    }

    static bool classof(TypeExpr const* t)
    {
        return t->kind == Kind;
    }
};

// ---------------------------------------------------------------------------
// Patterns. The same grammar that powers irrefutable `let` destructuring will
// later drive refutable `match` arms.
// ---------------------------------------------------------------------------

enum class PatternKind : std::uint8_t
{
    Ident,      // x
    Wildcard,   // _
    Tuple,      // (a, b)
    Array,      // [a, b, c]  (may contain one Rest element)
    Rest,       // ..  or  ..name   (only valid inside an array pattern)
};

struct Pattern
{
    PatternKind kind;
    SourceSpan span;

protected:
    constexpr Pattern(PatternKind k, SourceSpan s)
        : kind(k), span(s)
    {
    }
};

struct IdentPattern : Pattern
{
    static constexpr PatternKind Kind = PatternKind::Ident;
    Symbol name;
    Sym* sym = nullptr;         // binding introduced here (set by name resolution)

    constexpr IdentPattern(SourceSpan s, Symbol n)
        : Pattern(Kind, s), name(n)
    {
    }

    static bool classof(Pattern const* p)
    {
        return p->kind == Kind;
    }
};

struct WildcardPattern : Pattern
{
    static constexpr PatternKind Kind = PatternKind::Wildcard;

    constexpr explicit WildcardPattern(SourceSpan s)
        : Pattern(Kind, s)
    {
    }

    static bool classof(Pattern const* p)
    {
        return p->kind == Kind;
    }
};

struct TuplePattern : Pattern
{
    static constexpr PatternKind Kind = PatternKind::Tuple;
    std::span<Pattern*> elements;

    constexpr TuplePattern(SourceSpan s, std::span<Pattern*> elems)
        : Pattern(Kind, s), elements(elems)
    {
    }

    static bool classof(Pattern const* p)
    {
        return p->kind == Kind;
    }
};

struct ArrayPattern : Pattern
{
    static constexpr PatternKind Kind = PatternKind::Array;
    std::span<Pattern*> elements;       // at most one element is a RestPattern

    constexpr ArrayPattern(SourceSpan s, std::span<Pattern*> elems)
        : Pattern(Kind, s), elements(elems)
    {
    }

    static bool classof(Pattern const* p)
    {
        return p->kind == Kind;
    }
};

// `..` ignores; `..name` binds the gap as a fixed [T; N-K] array. name is
// kInvalid for the ignore-only form.
struct RestPattern : Pattern
{
    static constexpr PatternKind Kind = PatternKind::Rest;
    Symbol name = StringInterner::kInvalid;
    Sym* sym = nullptr;         // binding introduced by `..name` (name resolution)

    constexpr RestPattern(SourceSpan s, Symbol n)
        : Pattern(Kind, s), name(n)
    {
    }

    bool binds() const
    {
        return name != StringInterner::kInvalid;
    }

    static bool classof(Pattern const* p)
    {
        return p->kind == Kind;
    }
};

// ---------------------------------------------------------------------------
// Expressions.
// ---------------------------------------------------------------------------

enum class ExprKind : std::uint8_t
{
    NumberLiteral,
    BoolLiteral,
    StringLiteral,
    Name,
    Unary,
    Binary,
    Assign,
    Call,
    Index,
    Field,
    Cast,
    Range,
    Array,
    Tuple,
};

struct Expr
{
    ExprKind kind;
    SourceSpan span;
    Type const* type = nullptr;     // resolved type, filled by the type checker

protected:
    constexpr Expr(ExprKind k, SourceSpan s)
        : kind(k), span(s)
    {
    }
};

// The raw digits live in the source; we keep the span (so dumps can show the
// text) plus structural facts: integer vs. fractional, and any suffix.
struct NumberLiteral : Expr
{
    static constexpr ExprKind Kind = ExprKind::NumberLiteral;
    bool isFloat;
    NumSuffix suffix;

    constexpr NumberLiteral(SourceSpan s, bool fractional, NumSuffix suf)
        : Expr(Kind, s), isFloat(fractional), suffix(suf)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

struct BoolLiteral : Expr
{
    static constexpr ExprKind Kind = ExprKind::BoolLiteral;
    bool value;

    constexpr BoolLiteral(SourceSpan s, bool v)
        : Expr(Kind, s), value(v)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

// The interned symbol holds the unescaped/raw spelling; span covers the quotes.
struct StringLiteral : Expr
{
    static constexpr ExprKind Kind = ExprKind::StringLiteral;
    Symbol value;

    constexpr StringLiteral(SourceSpan s, Symbol v)
        : Expr(Kind, s), value(v)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

struct NameExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Name;
    Symbol name;
    Sym* sym = nullptr;         // what this name refers to (set by name resolution)

    constexpr NameExpr(SourceSpan s, Symbol n)
        : Expr(Kind, s), name(n)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

struct UnaryExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Unary;
    UnaryOp op;
    Expr* operand;

    constexpr UnaryExpr(SourceSpan s, UnaryOp o, Expr* sub)
        : Expr(Kind, s), op(o), operand(sub)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

struct BinaryExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Binary;
    BinaryOp op;
    Expr* lhs;
    Expr* rhs;

    constexpr BinaryExpr(SourceSpan s, BinaryOp o, Expr* l, Expr* r)
        : Expr(Kind, s), op(o), lhs(l), rhs(r)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

struct AssignExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Assign;
    AssignOp op;
    Expr* target;
    Expr* value;

    constexpr AssignExpr(SourceSpan s, AssignOp o, Expr* lhs, Expr* rhs)
        : Expr(Kind, s), op(o), target(lhs), value(rhs)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

struct CallExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Call;
    Expr* callee;
    std::span<Expr*> args;

    constexpr CallExpr(SourceSpan s, Expr* fn, std::span<Expr*> a)
        : Expr(Kind, s), callee(fn), args(a)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

// a[i] and the from-end form a[^k] (fromEnd == true).
struct IndexExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Index;
    Expr* base;
    Expr* index;
    bool fromEnd;

    constexpr IndexExpr(SourceSpan s, Expr* b, Expr* i, bool end)
        : Expr(Kind, s), base(b), index(i), fromEnd(end)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

// tuple field access: base.index (e.g. t.0)
struct FieldExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Field;
    Expr* base;
    std::uint32_t index;

    constexpr FieldExpr(SourceSpan s, Expr* b, std::uint32_t i)
        : Expr(Kind, s), base(b), index(i)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

// value as type
struct CastExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Cast;
    Expr* value;
    TypeExpr* type;

    constexpr CastExpr(SourceSpan s, Expr* v, TypeExpr* t)
        : Expr(Kind, s), value(v), type(t)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

// lo..hi (exclusive) or lo..=hi (inclusive).
struct RangeExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Range;
    Expr* lo;
    Expr* hi;
    bool inclusive;

    constexpr RangeExpr(SourceSpan s, Expr* l, Expr* h, bool incl)
        : Expr(Kind, s), lo(l), hi(h), inclusive(incl)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

struct ArrayExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Array;
    std::span<Expr*> elements;

    constexpr ArrayExpr(SourceSpan s, std::span<Expr*> elems)
        : Expr(Kind, s), elements(elems)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

struct TupleExpr : Expr
{
    static constexpr ExprKind Kind = ExprKind::Tuple;
    std::span<Expr*> elements;

    constexpr TupleExpr(SourceSpan s, std::span<Expr*> elems)
        : Expr(Kind, s), elements(elems)
    {
    }

    static bool classof(Expr const* e)
    {
        return e->kind == Kind;
    }
};

// ---------------------------------------------------------------------------
// Statements.
// ---------------------------------------------------------------------------

enum class StmtKind : std::uint8_t
{
    Let,
    Expr,
    Return,
    Break,
    Continue,
    If,
    While,
    For,
    Block,
};

struct Stmt
{
    StmtKind kind;
    SourceSpan span;

protected:
    constexpr Stmt(StmtKind k, SourceSpan s)
        : kind(k), span(s)
    {
    }
};

struct BlockStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::Block;
    std::span<Stmt*> statements;

    constexpr BlockStmt(SourceSpan s, std::span<Stmt*> stmts)
        : Stmt(Kind, s), statements(stmts)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

// `let p: T = init;` or `const p: T = init;`. type and init may be null.
struct LetStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::Let;
    Pattern* pattern;
    TypeExpr* type;
    Expr* init;
    bool isConst;

    constexpr LetStmt(
        SourceSpan s,
        Pattern* pat,
        TypeExpr* ty,
        Expr* value,
        bool constant
    )
        : Stmt(Kind, s), pattern(pat), type(ty), init(value), isConst(constant)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

struct ExprStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::Expr;
    Expr* expr;

    constexpr ExprStmt(SourceSpan s, Expr* e)
        : Stmt(Kind, s), expr(e)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

// `ret;` or `ret value;` -- value may be null.
struct ReturnStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::Return;
    Expr* value;

    constexpr ReturnStmt(SourceSpan s, Expr* v)
        : Stmt(Kind, s), value(v)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

struct BreakStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::Break;

    constexpr explicit BreakStmt(SourceSpan s)
        : Stmt(Kind, s)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

struct ContinueStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::Continue;

    constexpr explicit ContinueStmt(SourceSpan s)
        : Stmt(Kind, s)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

// `if cond { ... } else <elseBranch>`. elseBranch is null, a BlockStmt, or
// another IfStmt (for `else if`).
struct IfStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::If;
    Expr* cond;
    BlockStmt* thenBranch;
    Stmt* elseBranch;

    constexpr IfStmt(
        SourceSpan s,
        Expr* condition,
        BlockStmt* thenB,
        Stmt* elseB
    )
        : Stmt(Kind, s), cond(condition), thenBranch(thenB), elseBranch(elseB)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

struct WhileStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::While;
    Expr* cond;
    BlockStmt* body;

    constexpr WhileStmt(SourceSpan s, Expr* condition, BlockStmt* b)
        : Stmt(Kind, s), cond(condition), body(b)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

// `for <var> in <iter> { ... }` -- iter is typically a RangeExpr.
struct ForStmt : Stmt
{
    static constexpr StmtKind Kind = StmtKind::For;
    Pattern* var;
    Expr* iter;
    BlockStmt* body;

    constexpr ForStmt(
        SourceSpan s,
        Pattern* loopVar,
        Expr* iterable,
        BlockStmt* b
    )
        : Stmt(Kind, s), var(loopVar), iter(iterable), body(b)
    {
    }

    static bool classof(Stmt const* s)
    {
        return s->kind == Kind;
    }
};

// ---------------------------------------------------------------------------
// Declarations.
// ---------------------------------------------------------------------------

enum class DeclKind : std::uint8_t
{
    Fn,
    Const,
};

struct Decl
{
    DeclKind kind;
    SourceSpan span;
    Sym* sym = nullptr;             // binding for this declaration (name resolution)

protected:
    constexpr Decl(DeclKind k, SourceSpan s)
        : kind(k), span(s)
    {
    }
};

// A function parameter. Not a node (no kind tag): a plain record stored in an
// arena array on FnDecl.
struct Param
{
    Symbol name;
    TypeExpr* type;
    SourceSpan span;
    Sym* sym = nullptr;         // binding introduced by this param (name resolution)
};

// `fn name(params) -> ret { body }`. returnType is null for void.
struct FnDecl : Decl
{
    static constexpr DeclKind Kind = DeclKind::Fn;
    Symbol name;
    std::span<Param> params;
    TypeExpr* returnType;
    BlockStmt* body;

    constexpr FnDecl(
        SourceSpan s,
        Symbol n,
        std::span<Param> p,
        TypeExpr* ret,
        BlockStmt* b
    )
        : Decl(Kind, s), name(n), params(p), returnType(ret), body(b)
    {
    }

    static bool classof(Decl const* d)
    {
        return d->kind == Kind;
    }
};

// `const name: T = init;` at top level. type may be null.
struct ConstDecl : Decl
{
    static constexpr DeclKind Kind = DeclKind::Const;
    Symbol name;
    TypeExpr* type;
    Expr* init;

    constexpr ConstDecl(
        SourceSpan s,
        Symbol n,
        TypeExpr* ty,
        Expr* value
    )
        : Decl(Kind, s), name(n), type(ty), init(value)
    {
    }

    static bool classof(Decl const* d)
    {
        return d->kind == Kind;
    }
};

// ---------------------------------------------------------------------------
// The whole translation unit: the parser's root result.
// ---------------------------------------------------------------------------

struct Module
{
    std::span<Decl*> decls;
};

} // namespace iq