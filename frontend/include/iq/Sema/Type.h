#pragma once

#include "iq/AST/Arena.h"

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace iq
{

// A semantic type. Types are *canonical*: the TypeContext hands out exactly one
// object per distinct type, so type equality is pointer equality. Built as a
// single tagged struct (the set of type forms is small and closed).
struct Type
{
    enum class Kind : std::uint8_t
    {
        // Primitive singletons (one object each).
        I32, I64, U32, U64, F32, F64,
        Bool, String, Void,
        UntypedInt,     // an unsuffixed integer literal not yet pinned to a width
        UntypedFloat,   // an unsuffixed fractional literal not yet pinned
        Error,          // stand-in after a type error, to suppress cascades
        // Composite (interned on demand).
        Array,
        Tuple,
    };

    Kind kind;
    Type const* elem = nullptr;             // Array element
    std::uint64_t arrayLen = 0;             // Array length
    std::span<Type const* const> elems{};   // Tuple members

    explicit Type(Kind k)
        : kind(k)
    {
    }

    Type(Kind k, Type const* element, std::uint64_t len)
        : kind(k), elem(element), arrayLen(len)
    {
    }

    Type(Kind k, std::span<Type const* const> members)
        : kind(k), elems(members)
    {
    }
};

constexpr bool isIntegerKind(Type::Kind k)
{
    switch (k)
    {
    case Type::Kind::I32:
    case Type::Kind::I64:
    case Type::Kind::U32:
    case Type::Kind::U64: return true;
    default:             return false;
    }
}

constexpr bool isFloatKind(Type::Kind k)
{
    return k == Type::Kind::F32 || k == Type::Kind::F64;
}

inline bool isInteger(Type const* t) { return t && isIntegerKind(t->kind); }
inline bool isFloat(Type const* t)   { return t && isFloatKind(t->kind); }
inline bool isNumeric(Type const* t) { return isInteger(t) || isFloat(t); }
inline bool isUntyped(Type const* t)
{
    return t && (t->kind == Type::Kind::UntypedInt || t->kind == Type::Kind::UntypedFloat);
}

std::string typeToString(Type const* t);

// Owns and uniques all Type objects; keep it alive as long as the AST that
// references the types.
class TypeContext
{
public:
    TypeContext();

    Type const* primitive(Type::Kind k) const;

    Type const* i32() const  { return primitive(Type::Kind::I32); }
    Type const* i64() const  { return primitive(Type::Kind::I64); }
    Type const* f64() const  { return primitive(Type::Kind::F64); }
    Type const* boolType() const   { return primitive(Type::Kind::Bool); }
    Type const* stringType() const { return primitive(Type::Kind::String); }
    Type const* voidType() const   { return primitive(Type::Kind::Void); }
    Type const* errorType() const  { return primitive(Type::Kind::Error); }
    Type const* untypedInt() const   { return primitive(Type::Kind::UntypedInt); }
    Type const* untypedFloat() const { return primitive(Type::Kind::UntypedFloat); }

    Type const* arrayOf(Type const* elem, std::uint64_t len);
    Type const* tupleOf(std::span<Type const* const> elems);

private:
    static constexpr std::size_t kPrimitiveCount =
        static_cast<std::size_t>(Type::Kind::Error) + 1;

    Arena m_arena;
    std::array<Type const*, kPrimitiveCount> m_primitives{};
    std::map<std::pair<Type const*, std::uint64_t>, Type const*> m_arrays;
    std::map<std::vector<Type const*>, Type const*> m_tuples;
};

} // namespace iq