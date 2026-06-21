#pragma once

#include "iq/AST/AST.h"

#include <string>
#include <string_view>

namespace iq
{

class SourceManager;
class StringInterner;

// Emits the AST as a Graphviz DOT graph to stdout (the --dump-ast=dot output).
// No Graphviz library is linked: this writes plain text, which the caller pipes
// through the `dot` tool, e.g.
//     iq --dump-ast=dot prog.iq | dot -Tpng -o prog.png
//
// Each AST node becomes one graph node with a unique id; child links become
// labelled edges. Traversal is the same kind-switch + cast<> idiom as the text
// printer.
class DotPrinter
{
public:
    DotPrinter(
        SourceManager const& sm,
        StringInterner const& interner
    );

    void print(Module const& module);

private:
    int node(std::string_view label);
    void edge(
        int from,
        int to,
        std::string_view label = ""
    );

    int emitDecl(Decl const* decl);
    int emitStmt(Stmt const* stmt);
    int emitExpr(Expr const* expr);
    int emitType(TypeExpr const* type);
    int emitPattern(Pattern const* pattern);

    std::string escape(std::string_view text) const;
    std::string_view symbolName(Symbol symbol) const;
    std::string_view spanText(SourceSpan span) const;

    SourceManager const& m_sm;
    StringInterner const& m_interner;
    int m_next = 0;
};

} // namespace iq