#include "iq/Sema/TypeChecker.h"

#include "iq/AST/Casting.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Sema/Sym.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace iq
{

TypeChecker::TypeChecker(
    SourceManager const& sm,
    StringInterner const& interner,
    DiagnosticEngine& diags,
    TypeContext& types
)
    : m_sm(sm), m_interner(interner), m_diags(diags), m_types(types)
{
}

void TypeChecker::errorAt(SourceSpan span, std::string message)
{
    m_diags.error(span, std::move(message));
}

Type const* TypeChecker::defaulted(Type const* t) const
{
    if (t == m_types.untypedInt())
    {
        return m_types.i32();
    }
    if (t == m_types.untypedFloat())
    {
        return m_types.f64();
    }
    return t;
}

bool TypeChecker::assignable(Type const* from, Type const* to) const
{
    if (!from || !to)
    {
        return true;        // unresolved; another diagnostic already fired
    }
    if (from == m_types.errorType() || to == m_types.errorType())
    {
        return true;        // suppress cascades
    }
    if (from == to)
    {
        return true;
    }
    // Untyped literals adapt: an integer literal to any numeric type, a
    // fractional literal to any float type.
    if (from == m_types.untypedInt() && isNumeric(to))
    {
        return true;
    }
    if (from == m_types.untypedFloat() && isFloat(to))
    {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Type annotations
// ---------------------------------------------------------------------------

Type const* TypeChecker::resolveType(TypeExpr const* type)
{
    switch (type->kind)
    {
    case TypeKind::Named:
    {
        auto const* named = cast<NamedType>(type);
        std::string_view const name = m_interner.lookup(named->name);
        if (name == "i32")    { return m_types.i32(); }
        if (name == "i64")    { return m_types.primitive(Type::Kind::I64); }
        if (name == "u32")    { return m_types.primitive(Type::Kind::U32); }
        if (name == "u64")    { return m_types.primitive(Type::Kind::U64); }
        if (name == "f32")    { return m_types.primitive(Type::Kind::F32); }
        if (name == "f64")    { return m_types.f64(); }
        if (name == "bool")   { return m_types.boolType(); }
        if (name == "string") { return m_types.stringType(); }
        if (name == "void")   { return m_types.voidType(); }
        errorAt(named->span, std::format("unknown type '{}'", name));
        return m_types.errorType();
    }

    case TypeKind::Array:
    {
        auto const* arr = cast<ArrayType>(type);
        Type const* element = resolveType(arr->element);
        // v0: the length must be a plain integer literal.
        std::uint64_t len = 0;
        if (auto const* lit = dyn_cast<NumberLiteral>(arr->size); lit && !lit->isFloat)
        {
            std::string_view const digits = m_sm.spanText(lit->span);
            for (char const c : digits)
            {
                if (c < '0' || c > '9')
                {
                    break;
                }
                len = len * 10 + static_cast<std::uint64_t>(c - '0');
            }
        }
        else
        {
            errorAt(arr->size->span, "array length must be an integer literal");
        }
        return m_types.arrayOf(element, len);
    }

    case TypeKind::Tuple:
    {
        auto const* tup = cast<TupleType>(type);
        if (tup->elements.empty())
        {
            return m_types.voidType();       // () is the unit type
        }
        std::vector<Type const*> members;
        members.reserve(tup->elements.size());
        for (TypeExpr const* e : tup->elements)
        {
            members.push_back(resolveType(e));
        }
        return m_types.tupleOf(members);
    }
    }
    return m_types.errorType();
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

void TypeChecker::check(Module& module)
{
    // Pass 1: resolve every top-level signature so bodies can reference any
    // function or const regardless of order.
    for (Decl* decl : module.decls)
    {
        checkSignature(decl);
    }
    // Pass 2: check bodies and const initializers.
    for (Decl* decl : module.decls)
    {
        checkDecl(decl);
    }
}

void TypeChecker::checkSignature(Decl* decl)
{
    if (auto* fn = dyn_cast<FnDecl>(decl))
    {
        for (Param& param : fn->params)
        {
            Type const* t = param.type ? resolveType(param.type) : m_types.errorType();
            if (param.sym)
            {
                param.sym->type = t;
            }
        }
        return;
    }

    if (auto* c = dyn_cast<ConstDecl>(decl))
    {
        Type const* t = nullptr;
        if (c->type)
        {
            t = resolveType(c->type);
            if (c->init)
            {
                checkExpr(c->init, t);
            }
        }
        else if (c->init)
        {
            t = defaulted(infer(c->init));
        }
        else
        {
            t = m_types.errorType();
        }
        if (c->sym)
        {
            c->sym->type = t;
        }
    }
}

void TypeChecker::checkDecl(Decl* decl)
{
    if (auto* fn = dyn_cast<FnDecl>(decl))
    {
        checkFn(fn);
    }
    // const initializers were fully checked in checkSignature.
}

void TypeChecker::checkFn(FnDecl* fn)
{
    m_currentReturn = fn->returnType ? resolveType(fn->returnType) : m_types.voidType();
    checkBlock(fn->body);
    m_currentReturn = nullptr;
}

void TypeChecker::checkBlock(BlockStmt* block)
{
    for (Stmt* stmt : block->statements)
    {
        checkStmt(stmt);
    }
}

void TypeChecker::checkStmt(Stmt* stmt)
{
    switch (stmt->kind)
    {
    case StmtKind::Block:
        checkBlock(cast<BlockStmt>(stmt));
        return;

    case StmtKind::Let:
    {
        auto* let = cast<LetStmt>(stmt);
        Type const* valueType = nullptr;
        if (let->type)
        {
            Type const* declared = resolveType(let->type);
            if (let->init)
            {
                checkExpr(let->init, declared);
            }
            valueType = declared;
        }
        else if (let->init)
        {
            valueType = defaulted(infer(let->init));
        }
        else
        {
            errorAt(let->span, "a binding without an initializer needs a type annotation");
            valueType = m_types.errorType();
        }
        bindPattern(let->pattern, valueType);
        return;
    }

    case StmtKind::Expr:
        infer(cast<ExprStmt>(stmt)->expr);
        return;

    case StmtKind::Return:
    {
        auto* ret = cast<ReturnStmt>(stmt);
        Type const* expected = m_currentReturn ? m_currentReturn : m_types.voidType();
        if (ret->value)
        {
            if (expected == m_types.voidType())
            {
                errorAt(ret->span, "returning a value from a function with no return type");
                infer(ret->value);
            }
            else
            {
                checkExpr(ret->value, expected);
            }
        }
        else if (expected != m_types.voidType())
        {
            errorAt(ret->span, std::format(
                "expected a return value of type '{}'", typeToString(expected)));
        }
        return;
    }

    case StmtKind::Break:
    case StmtKind::Continue:
        return;

    case StmtKind::If:
    {
        auto* node = cast<IfStmt>(stmt);
        checkExpr(node->cond, m_types.boolType());
        checkBlock(node->thenBranch);
        if (node->elseBranch)
        {
            checkStmt(node->elseBranch);
        }
        return;
    }

    case StmtKind::While:
    {
        auto* node = cast<WhileStmt>(stmt);
        checkExpr(node->cond, m_types.boolType());
        checkBlock(node->body);
        return;
    }

    case StmtKind::For:
    {
        auto* node = cast<ForStmt>(stmt);
        Type const* elemType = m_types.errorType();
        if (auto* range = dyn_cast<RangeExpr>(node->iter))
        {
            Type const* bound = unifyNumeric(range->lo, range->hi, "range bounds");
            if (isFloat(bound) || bound == m_types.untypedFloat())
            {
                errorAt(node->iter->span, "range bounds must be integers");
                elemType = m_types.errorType();
            }
            else
            {
                elemType = defaulted(bound);
            }
            range->type = elemType;
        }
        else
        {
            errorAt(node->iter->span, "a for loop requires a range (lo..hi)");
            infer(node->iter);
        }
        bindPattern(node->var, elemType);
        checkBlock(node->body);
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

void TypeChecker::bindPattern(Pattern* pattern, Type const* type)
{
    switch (pattern->kind)
    {
    case PatternKind::Ident:
        if (Sym* s = cast<IdentPattern>(pattern)->sym)
        {
            s->type = type;
        }
        return;

    case PatternKind::Wildcard:
        return;

    case PatternKind::Rest:
        // A bare RestPattern is handled by its enclosing array pattern; reaching
        // here standalone means it bound nothing meaningful.
        if (Sym* s = cast<RestPattern>(pattern)->sym)
        {
            s->type = type;
        }
        return;

    case PatternKind::Tuple:
    {
        auto* tup = cast<TuplePattern>(pattern);
        if (type->kind != Type::Kind::Tuple)
        {
            if (type != m_types.errorType())
            {
                errorAt(pattern->span, std::format(
                    "cannot destructure '{}' as a tuple", typeToString(type)));
            }
            for (Pattern* p : tup->elements)
            {
                bindPattern(p, m_types.errorType());
            }
            return;
        }
        if (tup->elements.size() != type->elems.size())
        {
            errorAt(pattern->span, std::format(
                "tuple pattern has {} elements but the value has {}",
                tup->elements.size(), type->elems.size()));
        }
        for (std::size_t i = 0; i < tup->elements.size(); ++i)
        {
            Type const* sub = i < type->elems.size() ? type->elems[i] : m_types.errorType();
            bindPattern(tup->elements[i], sub);
        }
        return;
    }

    case PatternKind::Array:
    {
        auto* arr = cast<ArrayPattern>(pattern);
        if (type->kind != Type::Kind::Array)
        {
            if (type != m_types.errorType())
            {
                errorAt(pattern->span, std::format(
                    "cannot destructure '{}' as an array", typeToString(type)));
            }
            for (Pattern* p : arr->elements)
            {
                bindPattern(p, m_types.errorType());
            }
            return;
        }

        Type const* element = type->elem;

        // Count fixed (non-rest) elements; a rest binds the remaining slice.
        std::size_t fixed = 0;
        RestPattern* rest = nullptr;
        for (Pattern* p : arr->elements)
        {
            if (auto* r = dyn_cast<RestPattern>(p))
            {
                rest = r;
            }
            else
            {
                ++fixed;
            }
        }

        if (!rest && arr->elements.size() != type->arrayLen)
        {
            errorAt(pattern->span, std::format(
                "array pattern has {} elements but the value has {}",
                arr->elements.size(), type->arrayLen));
        }
        if (rest && fixed > type->arrayLen)
        {
            errorAt(pattern->span, std::format(
                "array pattern needs at least {} elements but the value has {}",
                fixed, type->arrayLen));
        }

        std::uint64_t const restLen =
            type->arrayLen > fixed ? type->arrayLen - fixed : 0;

        for (Pattern* p : arr->elements)
        {
            if (auto* r = dyn_cast<RestPattern>(p))
            {
                if (r->sym)
                {
                    r->sym->type = m_types.arrayOf(element, restLen);
                }
            }
            else
            {
                bindPattern(p, element);
            }
        }
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// Expressions: infer (synthesize)
// ---------------------------------------------------------------------------

Type const* TypeChecker::infer(Expr* expr)
{
    Type const* result = m_types.errorType();

    switch (expr->kind)
    {
    case ExprKind::NumberLiteral:
    {
        auto* lit = cast<NumberLiteral>(expr);
        switch (lit->suffix)
        {
        case NumSuffix::I32: result = m_types.i32(); break;
        case NumSuffix::I64: result = m_types.primitive(Type::Kind::I64); break;
        case NumSuffix::U32: result = m_types.primitive(Type::Kind::U32); break;
        case NumSuffix::U64: result = m_types.primitive(Type::Kind::U64); break;
        case NumSuffix::F32: result = m_types.primitive(Type::Kind::F32); break;
        case NumSuffix::F64: result = m_types.f64(); break;
        case NumSuffix::None:
            result = lit->isFloat ? m_types.untypedFloat() : m_types.untypedInt();
            break;
        }
        break;
    }

    case ExprKind::BoolLiteral:
        result = m_types.boolType();
        break;

    case ExprKind::StringLiteral:
        result = m_types.stringType();
        break;

    case ExprKind::Name:
    {
        auto* name = cast<NameExpr>(expr);
        if (Sym* s = name->sym)
        {
            if (s->kind == SymKind::Function || s->kind == SymKind::Builtin)
            {
                errorAt(name->span, std::format(
                    "'{}' is a function and cannot be used as a value",
                    m_interner.lookup(name->name)));
                result = m_types.errorType();
            }
            else
            {
                result = s->type ? s->type : m_types.errorType();
            }
        }
        break;
    }

    case ExprKind::Unary:
    {
        auto* un = cast<UnaryExpr>(expr);
        if (un->op == UnaryOp::Not)
        {
            checkExpr(un->operand, m_types.boolType());
            result = m_types.boolType();
        }
        else // Neg
        {
            Type const* t = infer(un->operand);
            if (!isNumeric(t) && !isUntyped(t) && t != m_types.errorType())
            {
                errorAt(un->span, std::format(
                    "unary '-' requires a numeric operand, found '{}'", typeToString(t)));
                result = m_types.errorType();
            }
            else
            {
                result = t;
            }
        }
        break;
    }

    case ExprKind::Binary:
        result = inferBinary(cast<BinaryExpr>(expr));
        break;

    case ExprKind::Assign:
    {
        auto* assign = cast<AssignExpr>(expr);
        Type const* target = infer(assign->target);

        if (auto* lhs = dyn_cast<NameExpr>(assign->target);
            lhs && lhs->sym && !lhs->sym->isMutable)
        {
            errorAt(assign->span, std::format(
                "cannot assign to immutable '{}'", m_interner.lookup(lhs->name)));
        }
        if (assign->op != AssignOp::Assign && !isNumeric(target) && target != m_types.errorType())
        {
            errorAt(assign->span, "compound assignment requires a numeric operand");
        }
        checkExpr(assign->value, target);
        result = target;
        break;
    }

    case ExprKind::Call:
        result = inferCall(cast<CallExpr>(expr));
        break;

    case ExprKind::Index:
    {
        auto* idx = cast<IndexExpr>(expr);
        Type const* base = infer(idx->base);
        Type const* indexType = defaulted(infer(idx->index));
        if (!isInteger(indexType) && indexType != m_types.errorType())
        {
            errorAt(idx->index->span, std::format(
                "array index must be an integer, found '{}'", typeToString(indexType)));
        }
        if (base->kind == Type::Kind::Array)
        {
            result = base->elem;
        }
        else if (base != m_types.errorType())
        {
            errorAt(idx->span, std::format(
                "cannot index a value of type '{}'", typeToString(base)));
        }
        break;
    }

    case ExprKind::Cast:
    {
        auto* cst = cast<CastExpr>(expr);
        Type const* from = infer(cst->value);
        Type const* to = resolveType(cst->type);
        bool const fromOk = isNumeric(from) || isUntyped(from) || from == m_types.errorType();
        bool const toOk = isNumeric(to) || to == m_types.errorType();
        if (!fromOk || !toOk)
        {
            errorAt(cst->span, std::format(
                "invalid cast from '{}' to '{}'", typeToString(from), typeToString(to)));
        }
        result = to;
        break;
    }

    case ExprKind::Range:
        // Ranges are only meaningful as a for-loop iterable; handled there.
        errorAt(expr->span, "a range is only valid as a for-loop iterable");
        infer(cast<RangeExpr>(expr)->lo);
        infer(cast<RangeExpr>(expr)->hi);
        result = m_types.errorType();
        break;

    case ExprKind::Array:
    {
        auto* arr = cast<ArrayExpr>(expr);
        if (arr->elements.empty())
        {
            errorAt(arr->span, "cannot infer the type of an empty array; add an annotation");
            result = m_types.errorType();
            break;
        }
        // Pick the first concrete element type, else default the untyped ones.
        Type const* element = nullptr;
        for (Expr* e : arr->elements)
        {
            Type const* t = infer(e);
            if (!isUntyped(t) && t != m_types.errorType())
            {
                element = t;
                break;
            }
        }
        if (!element)
        {
            element = defaulted(infer(arr->elements[0]));
        }
        for (Expr* e : arr->elements)
        {
            checkExpr(e, element);
        }
        result = m_types.arrayOf(element, arr->elements.size());
        break;
    }

    case ExprKind::Tuple:
    {
        auto* tup = cast<TupleExpr>(expr);
        if (tup->elements.empty())
        {
            result = m_types.voidType();        // () is unit
            break;
        }
        std::vector<Type const*> members;
        members.reserve(tup->elements.size());
        for (Expr* e : tup->elements)
        {
            members.push_back(defaulted(infer(e)));
        }
        result = m_types.tupleOf(members);
        break;
    }
    }

    expr->type = result;
    return result;
}

Type const* TypeChecker::inferBinary(BinaryExpr* bin)
{
    switch (bin->op)
    {
    case BinaryOp::And:
    case BinaryOp::Or:
        checkExpr(bin->lhs, m_types.boolType());
        checkExpr(bin->rhs, m_types.boolType());
        return m_types.boolType();

    case BinaryOp::Eq:
    case BinaryOp::Ne:
    case BinaryOp::Lt:
    case BinaryOp::Le:
    case BinaryOp::Gt:
    case BinaryOp::Ge:
        unifyNumeric(bin->lhs, bin->rhs, "comparison");
        return m_types.boolType();

    default: // arithmetic
        return unifyNumeric(bin->lhs, bin->rhs, "arithmetic");
    }
}

Type const* TypeChecker::unifyNumeric(Expr* lhs, Expr* rhs, std::string_view what)
{
    Type const* lt = infer(lhs);
    Type const* rt = infer(rhs);

    if (lt == m_types.errorType() || rt == m_types.errorType())
    {
        return m_types.errorType();
    }

    bool const lUntyped = isUntyped(lt);
    bool const rUntyped = isUntyped(rt);

    if (lUntyped && rUntyped)
    {
        return (lt == m_types.untypedFloat() || rt == m_types.untypedFloat())
            ? m_types.untypedFloat()
            : m_types.untypedInt();
    }
    if (lUntyped && isNumeric(rt))
    {
        checkExpr(lhs, rt);
        return rt;
    }
    if (rUntyped && isNumeric(lt))
    {
        checkExpr(rhs, lt);
        return lt;
    }
    if (isNumeric(lt) && isNumeric(rt))
    {
        if (lt == rt)
        {
            return lt;
        }
        errorAt(SourceSpan::merge(lhs->span, rhs->span), std::format(
            "{} operands have mismatched types '{}' and '{}'",
            what, typeToString(lt), typeToString(rt)));
        return m_types.errorType();
    }

    errorAt(SourceSpan::merge(lhs->span, rhs->span), std::format(
        "{} requires numeric operands, found '{}' and '{}'",
        what, typeToString(lt), typeToString(rt)));
    return m_types.errorType();
}

Type const* TypeChecker::inferCall(CallExpr* call)
{
    auto* callee = dyn_cast<NameExpr>(call->callee);
    Sym* sym = callee ? callee->sym : nullptr;

    if (sym && sym->kind == SymKind::Builtin)
    {
        // print(x): exactly one argument of any type, returns void.
        if (call->args.size() != 1)
        {
            errorAt(call->span, std::format(
                "'{}' expects 1 argument but got {}",
                m_interner.lookup(callee->name), call->args.size()));
        }
        for (Expr* arg : call->args)
        {
            defaulted(infer(arg));
        }
        call->type = m_types.voidType();
        return m_types.voidType();
    }

    if (sym && sym->kind == SymKind::Function)
    {
        auto* fn = cast<FnDecl>(sym->decl);
        if (call->args.size() != fn->params.size())
        {
            errorAt(call->span, std::format(
                "'{}' expects {} argument(s) but got {}",
                m_interner.lookup(callee->name), fn->params.size(), call->args.size()));
        }
        std::size_t const n = std::min(call->args.size(), fn->params.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            Type const* paramType = fn->params[i].sym ? fn->params[i].sym->type
                                                       : m_types.errorType();
            checkExpr(call->args[i], paramType);
        }
        Type const* ret = fn->returnType ? resolveType(fn->returnType) : m_types.voidType();
        call->type = ret;
        return ret;
    }

    if (callee)
    {
        errorAt(call->span, std::format(
            "'{}' is not a function", m_interner.lookup(callee->name)));
    }
    else
    {
        errorAt(call->span, "this expression is not callable");
        infer(call->callee);
    }
    return m_types.errorType();
}

// ---------------------------------------------------------------------------
// Expressions: check (push an expected type down)
// ---------------------------------------------------------------------------

Type const* TypeChecker::checkExpr(Expr* expr, Type const* expected)
{
    if (!expected || expected == m_types.errorType())
    {
        return infer(expr);
    }

    switch (expr->kind)
    {
    case ExprKind::NumberLiteral:
    {
        auto* lit = cast<NumberLiteral>(expr);
        Type const* synth = infer(lit);        // sets a provisional type
        if (isUntyped(synth))
        {
            if (lit->isFloat && !isFloat(expected))
            {
                errorAt(lit->span, std::format(
                    "a fractional literal cannot have type '{}'", typeToString(expected)));
                expr->type = m_types.errorType();
            }
            else if (!isNumeric(expected))
            {
                errorAt(lit->span, std::format(
                    "expected '{}', found a numeric literal", typeToString(expected)));
                expr->type = m_types.errorType();
            }
            else
            {
                expr->type = expected;          // pin the literal
            }
        }
        else if (!assignable(synth, expected))
        {
            // A suffixed literal that contradicts the expected type.
            errorAt(lit->span, std::format(
                "literal of type '{}' does not match expected type '{}'",
                typeToString(synth), typeToString(expected)));
            expr->type = m_types.errorType();
        }
        return expr->type;
    }

    case ExprKind::Unary:
        if (auto* un = cast<UnaryExpr>(expr); un->op == UnaryOp::Neg && isNumeric(expected))
        {
            checkExpr(un->operand, expected);
            expr->type = expected;
            return expected;
        }
        break;

    case ExprKind::Binary:
    {
        auto* bin = cast<BinaryExpr>(expr);
        bool const arithmetic =
            bin->op != BinaryOp::And && bin->op != BinaryOp::Or
            && bin->op != BinaryOp::Eq && bin->op != BinaryOp::Ne
            && bin->op != BinaryOp::Lt && bin->op != BinaryOp::Le
            && bin->op != BinaryOp::Gt && bin->op != BinaryOp::Ge;
        if (arithmetic && isNumeric(expected))
        {
            checkExpr(bin->lhs, expected);
            checkExpr(bin->rhs, expected);
            expr->type = expected;
            return expected;
        }
        break;
    }

    case ExprKind::Array:
        if (auto* arr = cast<ArrayExpr>(expr); expected->kind == Type::Kind::Array)
        {
            if (arr->elements.size() != expected->arrayLen)
            {
                errorAt(arr->span, std::format(
                    "expected an array of {} element(s), found {}",
                    expected->arrayLen, arr->elements.size()));
            }
            for (Expr* e : arr->elements)
            {
                checkExpr(e, expected->elem);
            }
            expr->type = expected;
            return expected;
        }
        break;

    case ExprKind::Tuple:
        if (auto* tup = cast<TupleExpr>(expr); expected->kind == Type::Kind::Tuple)
        {
            if (tup->elements.size() == expected->elems.size())
            {
                for (std::size_t i = 0; i < tup->elements.size(); ++i)
                {
                    checkExpr(tup->elements[i], expected->elems[i]);
                }
                expr->type = expected;
                return expected;
            }
        }
        break;

    default:
        break;
    }

    // Fallback: synthesize, then verify compatibility.
    Type const* synth = infer(expr);
    if (!assignable(synth, expected))
    {
        errorAt(expr->span, std::format(
            "expected '{}', found '{}'", typeToString(expected), typeToString(synth)));
        expr->type = m_types.errorType();
    }
    else if (isUntyped(synth))
    {
        expr->type = expected;
    }
    return expr->type;
}

} // namespace iq