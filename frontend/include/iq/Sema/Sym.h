#pragma once

#include "iq/Source/SourceLocation.h"
#include "iq/Support/StringInterner.h"

#include <cstdint>
#include <string_view>

namespace iq
{

using Symbol = StringInterner::Symbol;

struct Decl;
struct Type;

// What kind of entity a name binds to. Produced by name resolution; consumed by
// the type checker and later passes.
enum class SymKind : std::uint8_t
{
    Function,       // a top-level fn
    GlobalConst,    // a top-level const
    Param,          // a function parameter
    Local,          // a let / const binding inside a body
    LoopVar,        // a for-loop variable
    Builtin,        // a predeclared name (e.g. print)
};

// A resolved binding. Created once per declared name and pointed at by every
// NameExpr that refers to it. `type` stays null until the type checker fills it.
struct Sym
{
    Symbol name;
    SymKind kind;
    bool isMutable;
    SourceSpan span;        // where the name was declared
    Decl* decl;             // back-pointer for Function / GlobalConst, else null
    Type const* type;       // resolved type, filled in by the type checker

    Sym(
        Symbol n,
        SymKind k,
        bool mut,
        SourceSpan s,
        Decl* d = nullptr
    )
        : name(n), kind(k), isMutable(mut), span(s), decl(d), type(nullptr)
    {
    }
};

constexpr std::string_view symKindName(SymKind kind)
{
    switch (kind)
    {
    case SymKind::Function:    return "function";
    case SymKind::GlobalConst: return "global const";
    case SymKind::Param:       return "param";
    case SymKind::Local:       return "local";
    case SymKind::LoopVar:     return "loop var";
    case SymKind::Builtin:     return "builtin";
    }
    return "?";
}

} // namespace iq