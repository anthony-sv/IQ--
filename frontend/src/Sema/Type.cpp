#include "iq/Sema/Type.h"

#include <format>

namespace iq
{

TypeContext::TypeContext()
{
    for (std::size_t i = 0; i < kPrimitiveCount; ++i)
    {
        m_primitives[i] = m_arena.make<Type>(static_cast<Type::Kind>(i));
    }
}

Type const* TypeContext::primitive(Type::Kind k) const
{
    return m_primitives[static_cast<std::size_t>(k)];
}

Type const* TypeContext::arrayOf(Type const* elem, std::uint64_t len)
{
    auto const key = std::pair{ elem, len };
    if (auto it = m_arrays.find(key); it != m_arrays.end())
    {
        return it->second;
    }
    Type const* t = m_arena.make<Type>(Type::Kind::Array, elem, len);
    m_arrays.emplace(key, t);
    return t;
}

Type const* TypeContext::tupleOf(std::span<Type const* const> elems)
{
    std::vector<Type const*> key(elems.begin(), elems.end());
    if (auto it = m_tuples.find(key); it != m_tuples.end())
    {
        return it->second;
    }
    std::span<Type const*> stored = m_arena.makeArray<Type const*>(
        std::span<Type const* const>(key));
    Type const* t = m_arena.make<Type>(Type::Kind::Tuple, stored);
    m_tuples.emplace(std::move(key), t);
    return t;
}

std::string typeToString(Type const* t)
{
    if (!t)
    {
        return "{null}";
    }
    switch (t->kind)
    {
    case Type::Kind::I32:          return "i32";
    case Type::Kind::I64:          return "i64";
    case Type::Kind::U32:          return "u32";
    case Type::Kind::U64:          return "u64";
    case Type::Kind::F32:          return "f32";
    case Type::Kind::F64:          return "f64";
    case Type::Kind::Bool:         return "bool";
    case Type::Kind::String:       return "string";
    case Type::Kind::Void:         return "void";
    case Type::Kind::UntypedInt:   return "{untyped int}";
    case Type::Kind::UntypedFloat: return "{untyped float}";
    case Type::Kind::Error:        return "{error}";
    case Type::Kind::Array:
        return std::format("[{}; {}]", typeToString(t->elem), t->arrayLen);
    case Type::Kind::Tuple:
    {
        std::string out = "(";
        bool first = true;
        for (Type const* e : t->elems)
        {
            if (!first)
            {
                out += ", ";
            }
            out += typeToString(e);
            first = false;
        }
        out += ")";
        return out;
    }
    }
    return "{?}";
}

} // namespace iq