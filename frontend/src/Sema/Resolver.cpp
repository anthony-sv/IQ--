#include "iq/Sema/Resolver.h"

#include "iq/AST/Casting.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <format>

namespace iq
{

Resolver::Resolver(
    SourceManager const& sm,
    StringInterner& interner,
    DiagnosticEngine& diags,
    Arena& arena
)
    : m_sm(sm), m_interner(interner), m_diags(diags), m_arena(arena)
{
}

void Resolver::pushScope()
{
    m_scopes.emplace_back();
}

void Resolver::popScope()
{
    m_scopes.pop_back();
}

Sym* Resolver::declare(
    Symbol name,
    SymKind kind,
    SourceSpan span,
    bool isMutable,
    Decl* decl
)
{
    Sym* const sym = m_arena.make<Sym>(name, kind, isMutable, span, decl);

    Scope& current = m_scopes.back();
    auto const [it, inserted] = current.names.try_emplace(name, sym);
    if (!inserted)
    {
        m_diags.error(span, std::format(
            "redefinition of '{}'", m_interner.lookup(name)));
        // Keep the newest binding so later references still resolve to something.
        it->second = sym;
    }
    return sym;
}

Sym* Resolver::lookup(Symbol name) const
{
    for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it)
    {
        if (auto found = it->names.find(name); found != it->names.end())
        {
            return found->second;
        }
    }
    return nullptr;
}

void Resolver::resolve(Module& module)
{
    pushScope();        // builtin scope
    declare(m_interner.intern("print"), SymKind::Builtin, SourceSpan{}, false);

    pushScope();        // global scope

    // Pass 1: register every top-level name so bodies can refer forward.
    for (Decl* decl : module.decls)
    {
        if (auto* fn = dyn_cast<FnDecl>(decl))
        {
            decl->sym = declare(fn->name, SymKind::Function, fn->span, false, decl);
        }
        else if (auto* c = dyn_cast<ConstDecl>(decl))
        {
            decl->sym = declare(c->name, SymKind::GlobalConst, c->span, false, decl);
        }
    }

    // Pass 2: resolve the insides.
    for (Decl* decl : module.decls)
    {
        resolveDecl(decl);
    }

    popScope();
    popScope();
}

void Resolver::resolveDecl(Decl* decl)
{
    if (auto* fn = dyn_cast<FnDecl>(decl))
    {
        resolveFn(fn);
    }
    else if (auto* c = dyn_cast<ConstDecl>(decl))
    {
        if (c->type)
        {
            resolveType(c->type);
        }
        if (c->init)
        {
            resolveExpr(c->init);
        }
    }
}

void Resolver::resolveFn(FnDecl* fn)
{
    pushScope();        // parameter scope

    for (Param& param : fn->params)
    {
        if (param.type)
        {
            resolveType(param.type);
        }
        param.sym = declare(param.name, SymKind::Param, param.span, false);
    }

    if (fn->returnType)
    {
        resolveType(fn->returnType);
    }

    resolveBlock(fn->body);

    popScope();
}

void Resolver::resolveBlock(BlockStmt* block)
{
    pushScope();
    for (Stmt* stmt : block->statements)
    {
        resolveStmt(stmt);
    }
    popScope();
}

void Resolver::resolveStmt(Stmt* stmt)
{
    switch (stmt->kind)
    {
    case StmtKind::Block:
        resolveBlock(cast<BlockStmt>(stmt));
        return;

    case StmtKind::Let:
    {
        auto* let = cast<LetStmt>(stmt);
        // The initializer is resolved BEFORE the new binding is in scope, so
        // `let x = x;` refers to an outer x, not to itself.
        if (let->init)
        {
            resolveExpr(let->init);
        }
        if (let->type)
        {
            resolveType(let->type);
        }
        bindPattern(let->pattern, SymKind::Local, !let->isConst);
        return;
    }

    case StmtKind::Expr:
        resolveExpr(cast<ExprStmt>(stmt)->expr);
        return;

    case StmtKind::Return:
    {
        auto* ret = cast<ReturnStmt>(stmt);
        if (ret->value)
        {
            resolveExpr(ret->value);
        }
        return;
    }

    case StmtKind::Break:
        if (m_loopDepth == 0)
        {
            m_diags.error(stmt->span, "'break' outside of a loop");
        }
        return;

    case StmtKind::Continue:
        if (m_loopDepth == 0)
        {
            m_diags.error(stmt->span, "'continue' outside of a loop");
        }
        return;

    case StmtKind::If:
    {
        auto* node = cast<IfStmt>(stmt);
        resolveExpr(node->cond);
        resolveBlock(node->thenBranch);
        if (node->elseBranch)
        {
            resolveStmt(node->elseBranch);
        }
        return;
    }

    case StmtKind::While:
    {
        auto* node = cast<WhileStmt>(stmt);
        resolveExpr(node->cond);
        ++m_loopDepth;
        resolveBlock(node->body);
        --m_loopDepth;
        return;
    }

    case StmtKind::For:
    {
        auto* node = cast<ForStmt>(stmt);
        // The iterable is resolved in the enclosing scope, before the loop
        // variable exists.
        resolveExpr(node->iter);
        pushScope();
        bindPattern(node->var, SymKind::LoopVar, false);
        ++m_loopDepth;
        resolveBlock(node->body);
        --m_loopDepth;
        popScope();
        return;
    }
    }
}

void Resolver::resolveExpr(Expr* expr)
{
    switch (expr->kind)
    {
    case ExprKind::NumberLiteral:
    case ExprKind::BoolLiteral:
    case ExprKind::StringLiteral:
        return;

    case ExprKind::Name:
    {
        auto* name = cast<NameExpr>(expr);
        if (Sym* sym = lookup(name->name))
        {
            name->sym = sym;
        }
        else
        {
            m_diags.error(name->span, std::format(
                "use of undeclared name '{}'", m_interner.lookup(name->name)));
        }
        return;
    }

    case ExprKind::Unary:
        resolveExpr(cast<UnaryExpr>(expr)->operand);
        return;

    case ExprKind::Binary:
    {
        auto* bin = cast<BinaryExpr>(expr);
        resolveExpr(bin->lhs);
        resolveExpr(bin->rhs);
        return;
    }

    case ExprKind::Assign:
    {
        auto* assign = cast<AssignExpr>(expr);
        resolveExpr(assign->target);
        resolveExpr(assign->value);
        return;
    }

    case ExprKind::Call:
    {
        auto* call = cast<CallExpr>(expr);
        resolveExpr(call->callee);
        for (Expr* arg : call->args)
        {
            resolveExpr(arg);
        }
        return;
    }

    case ExprKind::Index:
    {
        auto* index = cast<IndexExpr>(expr);
        resolveExpr(index->base);
        resolveExpr(index->index);
        return;
    }

    case ExprKind::Field:
        resolveExpr(cast<FieldExpr>(expr)->base);
        return;

    case ExprKind::Cast:
    {
        auto* cst = cast<CastExpr>(expr);
        resolveExpr(cst->value);
        resolveType(cst->type);
        return;
    }

    case ExprKind::Range:
    {
        auto* range = cast<RangeExpr>(expr);
        resolveExpr(range->lo);
        resolveExpr(range->hi);
        return;
    }

    case ExprKind::Array:
        for (Expr* e : cast<ArrayExpr>(expr)->elements)
        {
            resolveExpr(e);
        }
        return;

    case ExprKind::Tuple:
        for (Expr* e : cast<TupleExpr>(expr)->elements)
        {
            resolveExpr(e);
        }
        return;
    }
}

void Resolver::resolveType(TypeExpr* type)
{
    switch (type->kind)
    {
    case TypeKind::Named:
        // Validating the type name itself is the type checker's job.
        return;

    case TypeKind::Array:
    {
        auto* arr = cast<ArrayType>(type);
        resolveType(arr->element);
        resolveExpr(arr->size);     // the length may reference a const
        return;
    }

    case TypeKind::Tuple:
        for (TypeExpr* e : cast<TupleType>(type)->elements)
        {
            resolveType(e);
        }
        return;
    }
}

void Resolver::bindPattern(
    Pattern* pattern,
    SymKind kind,
    bool isMutable
)
{
    switch (pattern->kind)
    {
    case PatternKind::Ident:
    {
        auto* ident = cast<IdentPattern>(pattern);
        ident->sym = declare(ident->name, kind, ident->span, isMutable);
        return;
    }

    case PatternKind::Wildcard:
        return;

    case PatternKind::Rest:
    {
        auto* rest = cast<RestPattern>(pattern);
        if (rest->binds())
        {
            rest->sym = declare(rest->name, kind, rest->span, isMutable);
        }
        return;
    }

    case PatternKind::Tuple:
        for (Pattern* p : cast<TuplePattern>(pattern)->elements)
        {
            bindPattern(p, kind, isMutable);
        }
        return;

    case PatternKind::Array:
        for (Pattern* p : cast<ArrayPattern>(pattern)->elements)
        {
            bindPattern(p, kind, isMutable);
        }
        return;
    }
}

} // namespace iq