#pragma once

#include "iq/AST/AST.h"
#include "iq/Sema/Type.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace iq
{

class SourceManager;
class StringInterner;
class DiagnosticEngine;

// Emits textual LLVM IR for the typed AST (run after the type checker). The IR
// is meant to be compiled by an external `clang`/`llc`:
//     iq --emit-llvm prog.iq > prog.ll && clang prog.ll -o prog.exe
//
// First cut: scalar features only (functions, integers/floats/bool, arithmetic,
// comparisons, if/while/for-over-range, calls, casts, and print -> printf).
// Arrays, tuples, indexing and destructuring report a "not yet supported"
// diagnostic instead of emitting wrong code.
class CodeGen
{
public:
    CodeGen(
        SourceManager const& sm,
        StringInterner const& interner,
        DiagnosticEngine& diags,
        TypeContext& types
    );

    // Returns the module IR text. Check diags.hasErrors() afterwards; on error
    // the result is incomplete and should not be used.
    std::string run(Module const& module);

private:
    std::string llvmType(Type const* t);
    std::string llvmReturnType(FnDecl const* fn);

    void emitConstGlobal(ConstDecl const* c);
    void emitFn(FnDecl const* fn);

    void emitStmt(Stmt const* stmt);
    void emitBlock(BlockStmt const* block);

    // Returns an IR operand (an SSA name like %t3, or a constant like 10).
    std::string emitExpr(Expr const* expr);
    std::string emitBinary(BinaryExpr const* bin);
    std::string emitShortCircuit(BinaryExpr const* bin);
    std::string emitCall(CallExpr const* call);
    std::string emitCast(CastExpr const* cst);
    void emitPrint(CallExpr const* call);

    // IR-building helpers.
    std::string freshTemp();
    std::string freshLabel(std::string_view hint);
    void label(std::string const& name);
    void line(std::string const& text);           // into the current block
    void terminate(std::string const& text);      // a terminator (br/ret/...)
    void br(std::string const& target);
    void condBr(
        std::string const& cond,
        std::string const& ifTrue,
        std::string const& ifFalse
    );

    std::string allocaFor(Sym const* sym);         // entry-block slot for a binding
    std::string addrOf(Sym const* sym) const;      // slot/global pointer name

    std::string numericLiteral(NumberLiteral const* lit) const;
    std::string internCString(std::string_view bytes);  // -> global ptr name

    void unsupported(SourceSpan span, std::string_view what);

    SourceManager const& m_sm;
    StringInterner const& m_interner;
    DiagnosticEngine& m_diags;
    TypeContext& m_types;

    std::string m_header;   // declares, format strings, const + string globals
    std::string m_funcs;    // function definitions
    std::string m_entry;    // current function's entry-block allocas
    std::string m_body;     // current function's instructions

    int m_temp = 0;
    int m_label = 0;
    int m_str = 0;
    bool m_terminated = false;
    bool m_inMain = false;

    std::unordered_map<Sym const*, std::string> m_globals;  // const Sym -> @name
    std::unordered_map<Sym const*, std::string> m_locals;   // local Sym -> %slot

    struct Loop
    {
        std::string continueTarget;
        std::string breakTarget;
    };
    std::vector<Loop> m_loops;
};

} // namespace iq