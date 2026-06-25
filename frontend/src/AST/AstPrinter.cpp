#include "iq/AST/AstPrinter.h"

#include "iq/AST/Casting.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <format>
#include <print>
#include <string>

namespace iq
{

namespace
{

// Raised on entry to a child scope, lowered on exit -- keeps indentation
// balanced even across early returns.
struct Indent
{
    int& depth;
    explicit Indent(int& d) : depth(d) { ++depth; }
    ~Indent() { --depth; }
};

} // namespace

AstPrinter::AstPrinter(
    SourceManager const& sm,
    StringInterner const& interner
)
    : m_sm(sm), m_interner(interner)
{
}

void AstPrinter::line(std::string_view text) const
{
    std::println("{}{}", std::string(static_cast<std::size_t>(m_depth) * 2, ' '), text);
}

std::string_view AstPrinter::symbolName(Symbol symbol) const
{
    if (symbol == StringInterner::kInvalid)
    {
        return "_";
    }
    return m_interner.lookup(symbol);
}

std::string_view AstPrinter::spanText(SourceSpan span) const
{
    return m_sm.spanText(span);
}

void AstPrinter::print(Module const& module)
{
    line("Module");
    Indent const indent(m_depth);
    for (Decl const* decl : module.decls)
    {
        printDecl(decl);
    }
}

void AstPrinter::printDecl(Decl const* decl)
{
    if (auto const* fn = dyn_cast<FnDecl>(decl))
    {
        line(std::format("FnDecl '{}'", symbolName(fn->name)));
        Indent const indent(m_depth);

        if (fn->params.empty())
        {
            line("params: (none)");
        }
        else
        {
            line("params:");
            Indent const p(m_depth);
            for (Param const& param : fn->params)
            {
                line(std::format("param '{}'", symbolName(param.name)));
                Indent const pt(m_depth);
                printType(param.type);
            }
        }

        if (fn->returnType)
        {
            line("returns:");
            Indent const r(m_depth);
            printType(fn->returnType);
        }
        else
        {
            line("returns: void");
        }

        printStmt(fn->body);
        return;
    }

    if (auto const* c = dyn_cast<ConstDecl>(decl))
    {
        line(std::format("ConstDecl '{}'", symbolName(c->name)));
        Indent const indent(m_depth);
        if (c->type)
        {
            line("type:");
            Indent const t(m_depth);
            printType(c->type);
        }
        printExpr(c->init);
        return;
    }

    line("<unknown decl>");
}

void AstPrinter::printStmt(Stmt const* stmt)
{
    switch (stmt->kind)
    {
    case StmtKind::Block:
    {
        auto const* b = cast<BlockStmt>(stmt);
        line("Block");
        Indent const indent(m_depth);
        for (Stmt const* s : b->statements)
        {
            printStmt(s);
        }
        return;
    }
    case StmtKind::Let:
    {
        auto const* l = cast<LetStmt>(stmt);
        line(l->isConst ? "ConstStmt" : "LetStmt");
        Indent const indent(m_depth);
        printPattern(l->pattern);
        if (l->type)
        {
            line("type:");
            Indent const t(m_depth);
            printType(l->type);
        }
        if (l->init)
        {
            line("init:");
            Indent const i(m_depth);
            printExpr(l->init);
        }
        return;
    }
    case StmtKind::Expr:
    {
        auto const* e = cast<ExprStmt>(stmt);
        line("ExprStmt");
        Indent const indent(m_depth);
        printExpr(e->expr);
        return;
    }
    case StmtKind::Return:
    {
        auto const* r = cast<ReturnStmt>(stmt);
        line("Return");
        if (r->value)
        {
            Indent const indent(m_depth);
            printExpr(r->value);
        }
        return;
    }
    case StmtKind::Break:
        line("Break");
        return;
    case StmtKind::Continue:
        line("Continue");
        return;
    case StmtKind::If:
    {
        auto const* i = cast<IfStmt>(stmt);
        line("If");
        Indent const indent(m_depth);
        line("cond:");
        {
            Indent const c(m_depth);
            printExpr(i->cond);
        }
        line("then:");
        {
            Indent const t(m_depth);
            printStmt(i->thenBranch);
        }
        if (i->elseBranch)
        {
            line("else:");
            Indent const e(m_depth);
            printStmt(i->elseBranch);
        }
        return;
    }
    case StmtKind::While:
    {
        auto const* w = cast<WhileStmt>(stmt);
        line("While");
        Indent const indent(m_depth);
        line("cond:");
        {
            Indent const c(m_depth);
            printExpr(w->cond);
        }
        printStmt(w->body);
        return;
    }
    case StmtKind::For:
    {
        auto const* f = cast<ForStmt>(stmt);
        line("For");
        Indent const indent(m_depth);
        line("var:");
        {
            Indent const v(m_depth);
            printPattern(f->var);
        }
        line("in:");
        {
            Indent const i(m_depth);
            printExpr(f->iter);
        }
        printStmt(f->body);
        return;
    }
    }
}

void AstPrinter::printExpr(Expr const* expr)
{
    switch (expr->kind)
    {
    case ExprKind::NumberLiteral:
    {
        auto const* n = cast<NumberLiteral>(expr);
        line(std::format(
            "Number {} ({})",
            spanText(n->span),
            n->isFloat ? "float" : "int"
        ));
        return;
    }
    case ExprKind::BoolLiteral:
        line(std::format("Bool {}", cast<BoolLiteral>(expr)->value ? "true" : "false"));
        return;
    case ExprKind::StringLiteral:
        line(std::format("String \"{}\"", symbolName(cast<StringLiteral>(expr)->value)));
        return;
    case ExprKind::Name:
        line(std::format("Name '{}'", symbolName(cast<NameExpr>(expr)->name)));
        return;
    case ExprKind::Unary:
    {
        auto const* u = cast<UnaryExpr>(expr);
        line(std::format("Unary '{}'", unaryOpName(u->op)));
        Indent const indent(m_depth);
        printExpr(u->operand);
        return;
    }
    case ExprKind::Binary:
    {
        auto const* b = cast<BinaryExpr>(expr);
        line(std::format("Binary '{}'", binaryOpName(b->op)));
        Indent const indent(m_depth);
        printExpr(b->lhs);
        printExpr(b->rhs);
        return;
    }
    case ExprKind::Assign:
    {
        auto const* a = cast<AssignExpr>(expr);
        line(std::format("Assign '{}'", assignOpName(a->op)));
        Indent const indent(m_depth);
        printExpr(a->target);
        printExpr(a->value);
        return;
    }
    case ExprKind::Call:
    {
        auto const* c = cast<CallExpr>(expr);
        line("Call");
        Indent const indent(m_depth);
        line("callee:");
        {
            Indent const ce(m_depth);
            printExpr(c->callee);
        }
        if (!c->args.empty())
        {
            line("args:");
            Indent const a(m_depth);
            for (Expr const* arg : c->args)
            {
                printExpr(arg);
            }
        }
        return;
    }
    case ExprKind::Index:
    {
        auto const* i = cast<IndexExpr>(expr);
        line(i->fromEnd ? "Index (from end)" : "Index");
        Indent const indent(m_depth);
        printExpr(i->base);
        printExpr(i->index);
        return;
    }
    case ExprKind::Cast:
    {
        auto const* c = cast<CastExpr>(expr);
        line("Cast");
        Indent const indent(m_depth);
        printExpr(c->value);
        line("as:");
        {
            Indent const t(m_depth);
            printType(c->type);
        }
        return;
    }
    case ExprKind::Range:
    {
        auto const* r = cast<RangeExpr>(expr);
        line(r->inclusive ? "Range (inclusive)" : "Range");
        Indent const indent(m_depth);
        printExpr(r->lo);
        printExpr(r->hi);
        return;
    }
    case ExprKind::Array:
    {
        auto const* a = cast<ArrayExpr>(expr);
        line("Array");
        Indent const indent(m_depth);
        for (Expr const* e : a->elements)
        {
            printExpr(e);
        }
        return;
    }
    case ExprKind::Tuple:
    {
        auto const* t = cast<TupleExpr>(expr);
        line("Tuple");
        Indent const indent(m_depth);
        for (Expr const* e : t->elements)
        {
            printExpr(e);
        }
        return;
    }
    }
}

void AstPrinter::printType(TypeExpr const* type)
{
    switch (type->kind)
    {
    case TypeKind::Named:
        line(std::format("NamedType '{}'", symbolName(cast<NamedType>(type)->name)));
        return;
    case TypeKind::Array:
    {
        auto const* a = cast<ArrayType>(type);
        line("ArrayType");
        Indent const indent(m_depth);
        line("element:");
        {
            Indent const e(m_depth);
            printType(a->element);
        }
        line("size:");
        {
            Indent const s(m_depth);
            printExpr(a->size);
        }
        return;
    }
    case TypeKind::Tuple:
    {
        auto const* t = cast<TupleType>(type);
        line(t->elements.empty() ? "TupleType (unit)" : "TupleType");
        Indent const indent(m_depth);
        for (TypeExpr const* e : t->elements)
        {
            printType(e);
        }
        return;
    }
    }
}

void AstPrinter::printPattern(Pattern const* pattern)
{
    switch (pattern->kind)
    {
    case PatternKind::Ident:
        line(std::format("IdentPattern '{}'", symbolName(cast<IdentPattern>(pattern)->name)));
        return;
    case PatternKind::Wildcard:
        line("WildcardPattern '_'");
        return;
    case PatternKind::Rest:
    {
        auto const* r = cast<RestPattern>(pattern);
        line(r->binds()
            ? std::format("RestPattern '..{}'", symbolName(r->name))
            : std::string("RestPattern '..'"));
        return;
    }
    case PatternKind::Tuple:
    {
        auto const* t = cast<TuplePattern>(pattern);
        line("TuplePattern");
        Indent const indent(m_depth);
        for (Pattern const* p : t->elements)
        {
            printPattern(p);
        }
        return;
    }
    case PatternKind::Array:
    {
        auto const* a = cast<ArrayPattern>(pattern);
        line("ArrayPattern");
        Indent const indent(m_depth);
        for (Pattern const* p : a->elements)
        {
            printPattern(p);
        }
        return;
    }
    }
}

} // namespace iq