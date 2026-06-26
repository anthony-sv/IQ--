#pragma once

#include "iq/AST/AST.h"
#include "iq/Sema/Type.h"

namespace iq
{

class SourceManager;
class StringInterner;
class DiagnosticEngine;

// The type-checking pass. Runs after name resolution (every NameExpr already
// has its Sym). Fills in expr->type for every expression and sym->type for
// every binding, and reports type errors.
//
// Inference is bidirectional: infer() synthesizes a type bottom-up, while
// check() pushes an expected type down. Unsuffixed numeric literals stay
// "untyped" until an expected type pins them; otherwise they default (i32/f64).
class TypeChecker
{
public:
    TypeChecker(
        SourceManager const& sm,
        StringInterner const& interner,
        DiagnosticEngine& diags,
        TypeContext& types
    );

    void check(Module& module);

private:
    // Map an AST type annotation to a canonical semantic type.
    Type const* resolveType(TypeExpr const* type);

    // Top-level signatures (param/return/const types) before any body.
    void checkSignature(Decl* decl);
    void checkDecl(Decl* decl);
    void checkFn(FnDecl* fn);

    void checkBlock(BlockStmt* block);
    void checkStmt(Stmt* stmt);

    // Bidirectional pair.
    Type const* infer(Expr* expr);
    Type const* checkExpr(Expr* expr, Type const* expected);

    Type const* inferCall(CallExpr* call);
    Type const* inferBinary(BinaryExpr* bin);

    // Distribute a value type onto a binding pattern (destructuring).
    void bindPattern(Pattern* pattern, Type const* type);

    // Pin two numeric operands to a common type (pins untyped literals); returns
    // the common concrete/untyped type, or the error type on mismatch.
    Type const* unifyNumeric(Expr* lhs, Expr* rhs, std::string_view what);

    // Replace an untyped literal type with its default (i32 / f64).
    Type const* defaulted(Type const* t) const;

    // Is a value of type `from` acceptable where `to` is expected?
    bool assignable(Type const* from, Type const* to) const;

    void errorAt(SourceSpan span, std::string message);

    SourceManager const& m_sm;
    StringInterner const& m_interner;
    DiagnosticEngine& m_diags;
    TypeContext& m_types;
    Type const* m_currentReturn = nullptr;  // expected return type of the fn in scope
};

} // namespace iq