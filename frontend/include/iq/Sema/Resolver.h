#pragma once

#include "iq/AST/AST.h"
#include "iq/AST/Arena.h"
#include "iq/Sema/Sym.h"

#include <unordered_map>
#include <vector>

namespace iq
{

class SourceManager;
class StringInterner;
class DiagnosticEngine;

// Name resolution: binds every NameExpr (and each binding site) to its Sym,
// using a stack of lexical scopes. Reports undeclared names, redefinitions in
// the same scope, and break/continue outside a loop.
//
// Top level is resolved in two passes so functions and consts may refer to each
// other regardless of source order; inside bodies, resolution is sequential, so
// using a local before its `let` is correctly an error.
class Resolver
{
public:
    Resolver(
        SourceManager const& sm,
        StringInterner& interner,
        DiagnosticEngine& diags,
        Arena& arena
    );

    void resolve(Module& module);

private:
    struct Scope
    {
        std::unordered_map<Symbol, Sym*> names;
    };

    void pushScope();
    void popScope();

    Sym* declare(
        Symbol name,
        SymKind kind,
        SourceSpan span,
        bool isMutable,
        Decl* decl = nullptr
    );
    Sym* lookup(Symbol name) const;

    void resolveDecl(Decl* decl);
    void resolveFn(FnDecl* fn);

    void resolveBlock(BlockStmt* block);
    void resolveStmt(Stmt* stmt);

    void resolveExpr(Expr* expr);
    void resolveType(TypeExpr* type);

    void bindPattern(
        Pattern* pattern,
        SymKind kind,
        bool isMutable
    );

    SourceManager const& m_sm;
    StringInterner& m_interner;
    DiagnosticEngine& m_diags;
    Arena& m_arena;
    std::vector<Scope> m_scopes;
    int m_loopDepth = 0;
};

} // namespace iq