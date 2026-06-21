#include "iq/AST/DotPrinter.h"

#include "iq/AST/Casting.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <format>
#include <print>

namespace iq
{

DotPrinter::DotPrinter(
    SourceManager const& sm,
    StringInterner const& interner
)
    : m_sm(sm), m_interner(interner)
{
}

std::string DotPrinter::escape(std::string_view text) const
{
    std::string out;
    out.reserve(text.size());
    for (char const c : text)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r':                break;
        default:   out += c;      break;
        }
    }
    return out;
}

std::string_view DotPrinter::symbolName(Symbol symbol) const
{
    if (symbol == StringInterner::kInvalid)
    {
        return "_";
    }
    return m_interner.lookup(symbol);
}

std::string_view DotPrinter::spanText(SourceSpan span) const
{
    return m_sm.spanText(span);
}

int DotPrinter::node(std::string_view label)
{
    int const id = m_next++;
    std::println("  n{} [label=\"{}\"];", id, escape(label));
    return id;
}

void DotPrinter::edge(
    int from,
    int to,
    std::string_view label
)
{
    if (label.empty())
    {
        std::println("  n{} -> n{};", from, to);
    }
    else
    {
        std::println("  n{} -> n{} [label=\"{}\"];", from, to, escape(label));
    }
}

void DotPrinter::print(Module const& module)
{
    std::println("digraph IQ_AST {{");
    std::println("  node [shape=box, fontname=\"Consolas\"];");
    std::println("  edge [fontname=\"Consolas\", fontsize=10];");

    int const root = node("Module");
    for (Decl const* decl : module.decls)
    {
        edge(root, emitDecl(decl));
    }

    std::println("}}");
}

int DotPrinter::emitDecl(Decl const* decl)
{
    if (auto const* fn = dyn_cast<FnDecl>(decl))
    {
        int const id = node(std::format("FnDecl '{}'", symbolName(fn->name)));
        for (Param const& param : fn->params)
        {
            int const p = node(std::format("param '{}'", symbolName(param.name)));
            edge(id, p, "param");
            edge(p, emitType(param.type), "type");
        }
        if (fn->returnType)
        {
            edge(id, emitType(fn->returnType), "returns");
        }
        edge(id, emitStmt(fn->body), "body");
        return id;
    }

    if (auto const* c = dyn_cast<ConstDecl>(decl))
    {
        int const id = node(std::format("ConstDecl '{}'", symbolName(c->name)));
        if (c->type)
        {
            edge(id, emitType(c->type), "type");
        }
        edge(id, emitExpr(c->init), "init");
        return id;
    }

    return node("<unknown decl>");
}

int DotPrinter::emitStmt(Stmt const* stmt)
{
    switch (stmt->kind)
    {
    case StmtKind::Block:
    {
        auto const* b = cast<BlockStmt>(stmt);
        int const id = node("Block");
        for (Stmt const* s : b->statements)
        {
            edge(id, emitStmt(s));
        }
        return id;
    }
    case StmtKind::Let:
    {
        auto const* l = cast<LetStmt>(stmt);
        int const id = node(l->isConst ? "ConstStmt" : "LetStmt");
        edge(id, emitPattern(l->pattern), "pat");
        if (l->type)
        {
            edge(id, emitType(l->type), "type");
        }
        if (l->init)
        {
            edge(id, emitExpr(l->init), "init");
        }
        return id;
    }
    case StmtKind::Expr:
    {
        auto const* e = cast<ExprStmt>(stmt);
        int const id = node("ExprStmt");
        edge(id, emitExpr(e->expr));
        return id;
    }
    case StmtKind::Return:
    {
        auto const* r = cast<ReturnStmt>(stmt);
        int const id = node("Return");
        if (r->value)
        {
            edge(id, emitExpr(r->value));
        }
        return id;
    }
    case StmtKind::Break:
        return node("Break");
    case StmtKind::Continue:
        return node("Continue");
    case StmtKind::If:
    {
        auto const* i = cast<IfStmt>(stmt);
        int const id = node("If");
        edge(id, emitExpr(i->cond), "cond");
        edge(id, emitStmt(i->thenBranch), "then");
        if (i->elseBranch)
        {
            edge(id, emitStmt(i->elseBranch), "else");
        }
        return id;
    }
    case StmtKind::While:
    {
        auto const* w = cast<WhileStmt>(stmt);
        int const id = node("While");
        edge(id, emitExpr(w->cond), "cond");
        edge(id, emitStmt(w->body), "body");
        return id;
    }
    case StmtKind::For:
    {
        auto const* f = cast<ForStmt>(stmt);
        int const id = node("For");
        edge(id, emitPattern(f->var), "var");
        edge(id, emitExpr(f->iter), "in");
        edge(id, emitStmt(f->body), "body");
        return id;
    }
    }
    return node("<unknown stmt>");
}

int DotPrinter::emitExpr(Expr const* expr)
{
    switch (expr->kind)
    {
    case ExprKind::NumberLiteral:
    {
        auto const* n = cast<NumberLiteral>(expr);
        return node(std::format(
            "Number {} ({})",
            spanText(n->span),
            n->isFloat ? "float" : "int"
        ));
    }
    case ExprKind::BoolLiteral:
        return node(std::format("Bool {}", cast<BoolLiteral>(expr)->value ? "true" : "false"));
    case ExprKind::StringLiteral:
        return node(std::format("String \"{}\"", symbolName(cast<StringLiteral>(expr)->value)));
    case ExprKind::Name:
        return node(std::format("Name '{}'", symbolName(cast<NameExpr>(expr)->name)));
    case ExprKind::Unary:
    {
        auto const* u = cast<UnaryExpr>(expr);
        int const id = node(std::format("Unary '{}'", unaryOpName(u->op)));
        edge(id, emitExpr(u->operand));
        return id;
    }
    case ExprKind::Binary:
    {
        auto const* b = cast<BinaryExpr>(expr);
        int const id = node(std::format("Binary '{}'", binaryOpName(b->op)));
        edge(id, emitExpr(b->lhs), "lhs");
        edge(id, emitExpr(b->rhs), "rhs");
        return id;
    }
    case ExprKind::Assign:
    {
        auto const* a = cast<AssignExpr>(expr);
        int const id = node(std::format("Assign '{}'", assignOpName(a->op)));
        edge(id, emitExpr(a->target), "target");
        edge(id, emitExpr(a->value), "value");
        return id;
    }
    case ExprKind::Call:
    {
        auto const* c = cast<CallExpr>(expr);
        int const id = node("Call");
        edge(id, emitExpr(c->callee), "callee");
        for (Expr const* arg : c->args)
        {
            edge(id, emitExpr(arg), "arg");
        }
        return id;
    }
    case ExprKind::Index:
    {
        auto const* i = cast<IndexExpr>(expr);
        int const id = node(i->fromEnd ? "Index (from end)" : "Index");
        edge(id, emitExpr(i->base), "base");
        edge(id, emitExpr(i->index), "index");
        return id;
    }
    case ExprKind::Cast:
    {
        auto const* c = cast<CastExpr>(expr);
        int const id = node("Cast");
        edge(id, emitExpr(c->value), "value");
        edge(id, emitType(c->type), "as");
        return id;
    }
    case ExprKind::Range:
    {
        auto const* r = cast<RangeExpr>(expr);
        int const id = node(r->inclusive ? "Range (inclusive)" : "Range");
        edge(id, emitExpr(r->lo), "lo");
        edge(id, emitExpr(r->hi), "hi");
        return id;
    }
    case ExprKind::Array:
    {
        auto const* a = cast<ArrayExpr>(expr);
        int const id = node("Array");
        for (Expr const* e : a->elements)
        {
            edge(id, emitExpr(e));
        }
        return id;
    }
    case ExprKind::Tuple:
    {
        auto const* t = cast<TupleExpr>(expr);
        int const id = node("Tuple");
        for (Expr const* e : t->elements)
        {
            edge(id, emitExpr(e));
        }
        return id;
    }
    }
    return node("<unknown expr>");
}

int DotPrinter::emitType(TypeExpr const* type)
{
    switch (type->kind)
    {
    case TypeKind::Named:
        return node(std::format("NamedType '{}'", symbolName(cast<NamedType>(type)->name)));
    case TypeKind::Array:
    {
        auto const* a = cast<ArrayType>(type);
        int const id = node("ArrayType");
        edge(id, emitType(a->element), "elem");
        edge(id, emitExpr(a->size), "size");
        return id;
    }
    case TypeKind::Tuple:
    {
        auto const* t = cast<TupleType>(type);
        int const id = node(t->elements.empty() ? "TupleType (unit)" : "TupleType");
        for (TypeExpr const* e : t->elements)
        {
            edge(id, emitType(e));
        }
        return id;
    }
    }
    return node("<unknown type>");
}

int DotPrinter::emitPattern(Pattern const* pattern)
{
    switch (pattern->kind)
    {
    case PatternKind::Ident:
        return node(std::format("IdentPattern '{}'", symbolName(cast<IdentPattern>(pattern)->name)));
    case PatternKind::Wildcard:
        return node("WildcardPattern '_'");
    case PatternKind::Rest:
    {
        auto const* r = cast<RestPattern>(pattern);
        return node(r->binds()
            ? std::format("RestPattern '..{}'", symbolName(r->name))
            : std::string("RestPattern '..'"));
    }
    case PatternKind::Tuple:
    {
        auto const* t = cast<TuplePattern>(pattern);
        int const id = node("TuplePattern");
        for (Pattern const* p : t->elements)
        {
            edge(id, emitPattern(p));
        }
        return id;
    }
    case PatternKind::Array:
    {
        auto const* a = cast<ArrayPattern>(pattern);
        int const id = node("ArrayPattern");
        for (Pattern const* p : a->elements)
        {
            edge(id, emitPattern(p));
        }
        return id;
    }
    }
    return node("<unknown pattern>");
}

} // namespace iq