#pragma once

#include "iq/AST/AST.h"

namespace iq
{

class SourceManager;
class StringInterner;

// Renders an AST as an indented text tree to stdout (the --dump-ast output).
// Traversal is a switch on each node's kind tag plus dyn_cast<> to the concrete
// type -- the LLVM-style RTTI in action. (A Graphviz DOT backend can follow.)
class AstPrinter
{
public:
    AstPrinter(
        SourceManager const& sm,
        StringInterner const& interner
    );

    void print(Module const& module);

private:
    void line(std::string_view text) const;

    void printDecl(Decl const* decl);
    void printStmt(Stmt const* stmt);
    void printExpr(Expr const* expr);
    void printType(TypeExpr const* type);
    void printPattern(Pattern const* pattern);

    std::string_view symbolName(Symbol symbol) const;
    std::string_view spanText(SourceSpan span) const;

    SourceManager const& m_sm;
    StringInterner const& m_interner;
    int m_depth = 0;
};

} // namespace iq