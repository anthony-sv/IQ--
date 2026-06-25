#pragma once

#include <cassert>

namespace iq
{

// LLVM-style RTTI helpers. Every node category (Expr, Stmt, ...) carries a
// `kind` tag, and each concrete node type exposes:
//     static constexpr <Cat>Kind Kind = ...;
//     static bool classof(<Cat> const* n) { return n->kind == Kind; }
//
// These three free functions are the whole RTTI surface -- the same idiom you
// will meet again as isa<>/cast<>/dyn_cast<> across the LLVM API.

// isa<T>(p): does p actually point at a T?
template <typename To, typename From>
bool isa(From const* p)
{
    return p != nullptr && To::classof(p);
}

// cast<T>(p): unconditional downcast, asserted in debug builds. Use when you
// already know the kind (e.g. right after an isa<> check or a kind switch).
template <typename To, typename From>
To* cast(From* p)
{
    assert(isa<To>(p) && "cast<> to an incompatible node kind");
    return static_cast<To*>(p);
}

template <typename To, typename From>
To const* cast(From const* p)
{
    assert(isa<To>(p) && "cast<> to an incompatible node kind");
    return static_cast<To const*>(p);
}

// dyn_cast<T>(p): downcast, or nullptr if p is not a T.
template <typename To, typename From>
To* dyn_cast(From* p)
{
    return isa<To>(p) ? static_cast<To*>(p) : nullptr;
}

template <typename To, typename From>
To const* dyn_cast(From const* p)
{
    return isa<To>(p) ? static_cast<To const*>(p) : nullptr;
}

} // namespace iq