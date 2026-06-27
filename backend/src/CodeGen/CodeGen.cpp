#include "iq/CodeGen/CodeGen.h"

#include "iq/AST/Casting.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Sema/Sym.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <format>

namespace iq
{

namespace
{

bool isSignedKind(Type::Kind k)
{
    return k == Type::Kind::I32 || k == Type::Kind::I64;
}

int intBits(Type::Kind k)
{
    switch (k)
    {
    case Type::Kind::I32:
    case Type::Kind::U32: return 32;
    case Type::Kind::I64:
    case Type::Kind::U64: return 64;
    default:              return 0;
    }
}

} // namespace

CodeGen::CodeGen(
    SourceManager const& sm,
    StringInterner const& interner,
    DiagnosticEngine& diags,
    TypeContext& types
)
    : m_sm(sm), m_interner(interner), m_diags(diags), m_types(types)
{
}

void CodeGen::unsupported(SourceSpan span, std::string_view what)
{
    m_diags.error(span, std::format("code generation does not support {} yet", what));
}

std::string CodeGen::llvmType(Type const* t)
{
    if (!t)
    {
        return "i32";
    }
    switch (t->kind)
    {
    case Type::Kind::I32:
    case Type::Kind::U32: return "i32";
    case Type::Kind::I64:
    case Type::Kind::U64: return "i64";
    case Type::Kind::F32: return "float";
    case Type::Kind::F64: return "double";
    case Type::Kind::Bool: return "i1";
    case Type::Kind::Void: return "void";
    case Type::Kind::String: return "ptr";
    case Type::Kind::UntypedInt: return "i32";
    case Type::Kind::UntypedFloat: return "double";
    case Type::Kind::Array:
        return std::format("[{} x {}]", t->arrayLen, llvmType(t->elem));
    case Type::Kind::Tuple:
    {
        std::string out = "{ ";
        bool first = true;
        for (Type const* e : t->elems)
        {
            if (!first) { out += ", "; }
            out += llvmType(e);
            first = false;
        }
        out += " }";
        return out;
    }
    default: return "i32";
    }
}

bool CodeGen::isAggregate(Type const* t)
{
    return t && (t->kind == Type::Kind::Array || t->kind == Type::Kind::Tuple);
}

// ---------------------------------------------------------------------------
// IR-building helpers
// ---------------------------------------------------------------------------

std::string CodeGen::freshTemp()
{
    return std::format("%t{}", m_temp++);
}

std::string CodeGen::freshLabel(std::string_view hint)
{
    return std::format("{}{}", hint, m_label++);
}

void CodeGen::label(std::string const& name)
{
    m_body += std::format("{}:\n", name);
    m_terminated = false;
}

void CodeGen::line(std::string const& text)
{
    if (m_terminated)
    {
        return;     // unreachable; drop
    }
    m_body += std::format("  {}\n", text);
}

void CodeGen::terminate(std::string const& text)
{
    if (m_terminated)
    {
        return;
    }
    m_body += std::format("  {}\n", text);
    m_terminated = true;
}

void CodeGen::br(std::string const& target)
{
    terminate(std::format("br label %{}", target));
}

void CodeGen::condBr(
    std::string const& cond,
    std::string const& ifTrue,
    std::string const& ifFalse
)
{
    terminate(std::format("br i1 {}, label %{}, label %{}", cond, ifTrue, ifFalse));
}

std::string CodeGen::allocaFor(Sym const* sym)
{
    if (auto it = m_locals.find(sym); it != m_locals.end())
    {
        return it->second;
    }
    std::string slot = std::format("%{}.{}", m_interner.lookup(sym->name), m_temp++);
    m_entry += std::format("  {} = alloca {}\n", slot, llvmType(sym->type));
    m_locals.emplace(sym, slot);
    return slot;
}

std::string CodeGen::addrOf(Sym const* sym) const
{
    if (auto it = m_locals.find(sym); it != m_locals.end())
    {
        return it->second;
    }
    if (auto it = m_globals.find(sym); it != m_globals.end())
    {
        return it->second;
    }
    return "%bad.addr";
}

// A fresh, unnamed-purpose stack slot in the entry block (for aggregate temps).
std::string CodeGen::freshAlloca(std::string const& llTy)
{
    std::string name = std::format("%agg{}", m_temp++);
    m_entry += std::format("  {} = alloca {}\n", name, llTy);
    return name;
}

// Copy a whole aggregate (array/tuple) from one slot to another using a
// first-class aggregate load/store -- no memcpy intrinsic or size math needed.
void CodeGen::copyAggregate(Type const* t, std::string const& src, std::string const& dst)
{
    std::string ty = llvmType(t);
    std::string v = freshTemp();
    line(std::format("{} = load {}, ptr {}", v, ty, src));
    line(std::format("store {} {}, ptr {}", ty, v, dst));
}

// Pointer to element `index` of an array at `base`.
std::string CodeGen::gepElement(
    Type const* arrayType,
    std::string const& base,
    std::string const& indexTy,
    std::string const& index
)
{
    std::string p = freshTemp();
    line(std::format("{} = getelementptr {}, ptr {}, i64 0, {} {}",
                     p, llvmType(arrayType), base, indexTy, index));
    return p;
}

// Pointer to field `field` of a tuple struct at `base`.
std::string CodeGen::gepField(Type const* tupleType, std::string const& base, std::size_t field)
{
    std::string p = freshTemp();
    line(std::format("{} = getelementptr {}, ptr {}, i64 0, i32 {}",
                     p, llvmType(tupleType), base, field));
    return p;
}

// Store a value (scalar) or copy an aggregate (from a source pointer) into dst.
void CodeGen::storeInto(Type const* t, std::string const& valueOrPtr, std::string const& dst)
{
    if (isAggregate(t))
    {
        copyAggregate(t, valueOrPtr, dst);
    }
    else
    {
        line(std::format("store {} {}, ptr {}", llvmType(t), valueOrPtr, dst));
    }
}

// Bind a (possibly nested) pattern against a value of type `type` living at
// `src`, allocating slots and copying out each piece. Powers destructuring.
void CodeGen::bindPatternFromPtr(Pattern const* pattern, Type const* type, std::string const& src)
{
    switch (pattern->kind)
    {
    case PatternKind::Wildcard:
        return;

    case PatternKind::Ident:
    {
        Sym const* sym = cast<IdentPattern>(pattern)->sym;
        std::string slot = allocaFor(sym);
        if (isAggregate(type))
        {
            copyAggregate(type, src, slot);
        }
        else
        {
            std::string v = freshTemp();
            line(std::format("{} = load {}, ptr {}", v, llvmType(type), src));
            line(std::format("store {} {}, ptr {}", llvmType(type), v, slot));
        }
        return;
    }

    case PatternKind::Tuple:
    {
        auto const* tup = cast<TuplePattern>(pattern);
        for (std::size_t i = 0; i < tup->elements.size() && i < type->elems.size(); ++i)
        {
            std::string fieldPtr = gepField(type, src, i);
            bindPatternFromPtr(tup->elements[i], type->elems[i], fieldPtr);
        }
        return;
    }

    case PatternKind::Array:
    {
        auto const* arr = cast<ArrayPattern>(pattern);
        Type const* elem = type->elem;
        std::uint64_t const n = type->arrayLen;

        std::size_t fixed = 0;
        for (Pattern const* p : arr->elements)
        {
            if (!isa<RestPattern>(p)) { ++fixed; }
        }
        std::uint64_t const restLen = n > fixed ? n - fixed : 0;

        std::uint64_t srcIndex = 0;
        for (Pattern const* p : arr->elements)
        {
            if (auto const* rest = dyn_cast<RestPattern>(p))
            {
                if (rest->sym)
                {
                    // Load the contiguous [restLen x elem] slice and copy it out.
                    std::string slicePtr = gepElement(type, src, "i64", std::to_string(srcIndex));
                    std::string sliceTy = std::format("[{} x {}]", restLen, llvmType(elem));
                    std::string slot = allocaFor(rest->sym);
                    std::string v = freshTemp();
                    line(std::format("{} = load {}, ptr {}", v, sliceTy, slicePtr));
                    line(std::format("store {} {}, ptr {}", sliceTy, v, slot));
                }
                srcIndex += restLen;
            }
            else
            {
                std::string elemPtr = gepElement(type, src, "i64", std::to_string(srcIndex));
                bindPatternFromPtr(p, elem, elemPtr);
                ++srcIndex;
            }
        }
        return;
    }

    case PatternKind::Rest:
        return;     // handled within the enclosing array pattern
    }
}

std::string CodeGen::numericLiteral(NumberLiteral const* lit) const
{
    std::string_view text = m_sm.spanText(lit->span);
    std::size_t end = 0;
    while (end < text.size()
        && ((text[end] >= '0' && text[end] <= '9') || text[end] == '.'))
    {
        ++end;
    }
    return std::string(text.substr(0, end));
}

std::string CodeGen::internCString(std::string_view bytes)
{
    std::string escaped;
    std::size_t len = 0;
    for (char const c : bytes)
    {
        unsigned char const u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u < 0x7f && u != '"' && u != '\\')
        {
            escaped += c;
        }
        else
        {
            escaped += std::format("\\{:02X}", u);
        }
        ++len;
    }
    escaped += "\\00";
    ++len;

    std::string name = std::format("@.str.{}", m_str++);
    m_header += std::format(
        "{} = private unnamed_addr constant [{} x i8] c\"{}\"\n", name, len, escaped);
    return name;
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

std::string CodeGen::run(Module const& module)
{
    m_header += "; LLVM IR generated by the IQ-- compiler\n\n";
    m_header += "declare i32 @printf(ptr, ...)\n\n";

    // printf format strings (newline-terminated, like the language's print).
    m_header += "@.fmt.i32 = private unnamed_addr constant [4 x i8] c\"%d\\0A\\00\"\n";
    m_header += "@.fmt.u32 = private unnamed_addr constant [4 x i8] c\"%u\\0A\\00\"\n";
    m_header += "@.fmt.i64 = private unnamed_addr constant [6 x i8] c\"%lld\\0A\\00\"\n";
    m_header += "@.fmt.u64 = private unnamed_addr constant [6 x i8] c\"%llu\\0A\\00\"\n";
    m_header += "@.fmt.f64 = private unnamed_addr constant [4 x i8] c\"%f\\0A\\00\"\n";
    m_header += "@.fmt.str = private unnamed_addr constant [4 x i8] c\"%s\\0A\\00\"\n\n";

    for (Decl const* decl : module.decls)
    {
        if (auto const* c = dyn_cast<ConstDecl>(decl))
        {
            emitConstGlobal(c);
        }
    }

    for (Decl const* decl : module.decls)
    {
        if (auto const* fn = dyn_cast<FnDecl>(decl))
        {
            emitFn(fn);
        }
    }

    return m_header + "\n" + m_funcs;
}

void CodeGen::emitConstGlobal(ConstDecl const* c)
{
    if (!c->sym)
    {
        return;
    }
    std::string name = std::format("@{}", m_interner.lookup(c->sym->name));
    std::string ty = llvmType(c->sym->type);

    std::string value = "0";
    if (auto const* lit = dyn_cast<NumberLiteral>(c->init))
    {
        value = numericLiteral(lit);
    }
    else if (auto const* b = dyn_cast<BoolLiteral>(c->init))
    {
        value = b->value ? "1" : "0";
    }
    else
    {
        unsupported(c->span, "a const initialized by a non-literal");
    }

    m_header += std::format("{} = constant {} {}\n", name, ty, value);
    m_globals.emplace(c->sym, name);
}

std::string CodeGen::llvmReturnType(FnDecl const* fn)
{
    // The type checker stored the resolved return type on the function's Sym.
    if (fn->sym && fn->sym->type)
    {
        return llvmType(fn->sym->type);
    }
    return fn->returnType ? "i32" : "void";
}

void CodeGen::emitFn(FnDecl const* fn)
{
    m_entry.clear();
    m_body.clear();
    m_locals.clear();
    m_loops.clear();
    m_temp = 0;
    m_label = 0;
    m_terminated = false;

    std::string const fnName = std::string(m_interner.lookup(fn->name));
    m_inMain = (fnName == "main");

    // main is lowered to `i32 @main` regardless of its void source signature.
    std::string retIr = m_inMain ? "i32" : llvmReturnType(fn);

    // Build parameter list and stash incoming values into entry-block slots.
    std::string params;
    for (std::size_t i = 0; i < fn->params.size(); ++i)
    {
        Param const& p = fn->params[i];
        std::string pty = llvmType(p.sym ? p.sym->type : nullptr);
        std::string incoming = std::format("%{}", m_interner.lookup(p.name));
        if (i != 0)
        {
            params += ", ";
        }
        params += std::format("{} {}", pty, incoming);

        std::string slot = allocaFor(p.sym);
        m_entry += std::format("  store {} {}, ptr {}\n", pty, incoming, slot);
    }

    emitBlock(fn->body);

    if (!m_terminated)
    {
        if (m_inMain)
        {
            terminate("ret i32 0");
        }
        else if (retIr == "void")
        {
            terminate("ret void");
        }
        else
        {
            terminate("unreachable");
        }
    }

    m_funcs += std::format("define {} @{}({}) {{\n", retIr, fnName, params);
    m_funcs += "entry:\n";
    m_funcs += m_entry;
    m_funcs += m_body;
    m_funcs += "}\n\n";
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

void CodeGen::emitBlock(BlockStmt const* block)
{
    for (Stmt const* s : block->statements)
    {
        emitStmt(s);
    }
}

void CodeGen::emitStmt(Stmt const* stmt)
{
    switch (stmt->kind)
    {
    case StmtKind::Block:
        emitBlock(cast<BlockStmt>(stmt));
        return;

    case StmtKind::Let:
    {
        auto const* let = cast<LetStmt>(stmt);
        if (!let->init)
        {
            return;     // declaration without initializer: nothing to emit
        }
        Type const* valueType = let->init->type;

        if (auto const* ident = dyn_cast<IdentPattern>(let->pattern))
        {
            std::string slot = allocaFor(ident->sym);
            std::string v = emitExpr(let->init);
            storeInto(valueType, v, slot);
        }
        else
        {
            // Destructuring: materialize the source aggregate, then copy pieces.
            std::string src = emitExpr(let->init);
            bindPatternFromPtr(let->pattern, valueType, src);
        }
        return;
    }

    case StmtKind::Expr:
        emitExpr(cast<ExprStmt>(stmt)->expr);
        return;

    case StmtKind::Return:
    {
        auto const* ret = cast<ReturnStmt>(stmt);
        if (m_inMain)
        {
            terminate("ret i32 0");
        }
        else if (ret->value)
        {
            std::string ty = llvmType(ret->value->type);
            if (isAggregate(ret->value->type))
            {
                // Aggregates are returned by value: load from their slot, ret it.
                std::string ptr = emitExpr(ret->value);
                std::string v = freshTemp();
                line(std::format("{} = load {}, ptr {}", v, ty, ptr));
                terminate(std::format("ret {} {}", ty, v));
            }
            else
            {
                std::string v = emitExpr(ret->value);
                terminate(std::format("ret {} {}", ty, v));
            }
        }
        else
        {
            terminate("ret void");
        }
        return;
    }

    case StmtKind::Break:
        if (!m_loops.empty())
        {
            br(m_loops.back().breakTarget);
        }
        return;

    case StmtKind::Continue:
        if (!m_loops.empty())
        {
            br(m_loops.back().continueTarget);
        }
        return;

    case StmtKind::If:
    {
        auto const* node = cast<IfStmt>(stmt);
        std::string cond = emitExpr(node->cond);
        std::string thenL = freshLabel("then");
        std::string endL = freshLabel("endif");

        if (node->elseBranch)
        {
            std::string elseL = freshLabel("else");
            condBr(cond, thenL, elseL);

            label(thenL);
            emitStmt(node->thenBranch);
            bool const thenTerm = m_terminated;
            br(endL);

            label(elseL);
            emitStmt(node->elseBranch);
            bool const elseTerm = m_terminated;
            br(endL);

            if (thenTerm && elseTerm)
            {
                m_terminated = true;    // both arms diverged; endL is unreachable
            }
            else
            {
                label(endL);
            }
        }
        else
        {
            condBr(cond, thenL, endL);
            label(thenL);
            emitStmt(node->thenBranch);
            br(endL);
            label(endL);
        }
        return;
    }

    case StmtKind::While:
    {
        auto const* node = cast<WhileStmt>(stmt);
        std::string condL = freshLabel("while.cond");
        std::string bodyL = freshLabel("while.body");
        std::string endL = freshLabel("while.end");

        br(condL);
        label(condL);
        std::string cond = emitExpr(node->cond);
        condBr(cond, bodyL, endL);

        m_loops.push_back({ condL, endL });
        label(bodyL);
        emitStmt(node->body);
        br(condL);
        m_loops.pop_back();

        label(endL);
        return;
    }

    case StmtKind::For:
    {
        auto const* node = cast<ForStmt>(stmt);
        auto const* range = dyn_cast<RangeExpr>(node->iter);
        auto const* ident = dyn_cast<IdentPattern>(node->var);
        if (!range || !ident)
        {
            unsupported(node->span, "this kind of for loop");
            return;
        }

        Type const* elemType = ident->sym->type;
        std::string ty = llvmType(elemType);
        bool const signedCmp = isSignedKind(elemType->kind);
        std::string const ltPred = range->inclusive
            ? (signedCmp ? "sle" : "ule")
            : (signedCmp ? "slt" : "ult");

        std::string slot = allocaFor(ident->sym);
        std::string lo = emitExpr(range->lo);
        std::string hi = emitExpr(range->hi);
        line(std::format("store {} {}, ptr {}", ty, lo, slot));

        std::string condL = freshLabel("for.cond");
        std::string bodyL = freshLabel("for.body");
        std::string incL = freshLabel("for.inc");
        std::string endL = freshLabel("for.end");

        br(condL);
        label(condL);
        std::string cur = freshTemp();
        line(std::format("{} = load {}, ptr {}", cur, ty, slot));
        std::string cmp = freshTemp();
        line(std::format("{} = icmp {} {} {}, {}", cmp, ltPred, ty, cur, hi));
        condBr(cmp, bodyL, endL);

        m_loops.push_back({ incL, endL });
        label(bodyL);
        emitStmt(node->body);
        br(incL);
        m_loops.pop_back();

        label(incL);
        std::string iv = freshTemp();
        line(std::format("{} = load {}, ptr {}", iv, ty, slot));
        std::string next = freshTemp();
        line(std::format("{} = add {} {}, 1", next, ty, iv));
        line(std::format("store {} {}, ptr {}", ty, next, slot));
        br(condL);

        label(endL);
        return;
    }
    }
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

std::string CodeGen::emitExpr(Expr const* expr)
{
    switch (expr->kind)
    {
    case ExprKind::NumberLiteral:
        return numericLiteral(cast<NumberLiteral>(expr));

    case ExprKind::BoolLiteral:
        return cast<BoolLiteral>(expr)->value ? "true" : "false";

    case ExprKind::StringLiteral:
        return internCString(m_interner.lookup(cast<StringLiteral>(expr)->value));

    case ExprKind::Name:
    {
        auto const* name = cast<NameExpr>(expr);
        std::string addr = addrOf(name->sym);
        if (isAggregate(name->type))
        {
            return addr;        // the slot itself is the aggregate (a pointer)
        }
        std::string t = freshTemp();
        line(std::format("{} = load {}, ptr {}", t, llvmType(name->type), addr));
        return t;
    }

    case ExprKind::Unary:
    {
        auto const* un = cast<UnaryExpr>(expr);
        std::string v = emitExpr(un->operand);
        std::string ty = llvmType(un->type);
        std::string t = freshTemp();
        if (un->op == UnaryOp::Not)
        {
            line(std::format("{} = xor i1 {}, true", t, v));
        }
        else if (isFloat(un->type))
        {
            line(std::format("{} = fneg {} {}", t, ty, v));
        }
        else
        {
            line(std::format("{} = sub {} 0, {}", t, ty, v));
        }
        return t;
    }

    case ExprKind::Binary:
        return emitBinary(cast<BinaryExpr>(expr));

    case ExprKind::Assign:
    {
        auto const* assign = cast<AssignExpr>(expr);

        // Parallel assignment: (a, b, ...) = (...). Materialize the whole RHS
        // FIRST, then store each field, so `(a, b) = (b, a)` swaps correctly.
        if (auto const* tup = dyn_cast<TupleExpr>(assign->target))
        {
            Type const* tupleType = assign->value->type;
            std::string src = emitExpr(assign->value);      // RHS fully built
            for (std::size_t i = 0; i < tup->elements.size(); ++i)
            {
                std::string dst = emitLValueAddr(tup->elements[i]);
                if (dst.empty())
                {
                    continue;
                }
                Type const* fieldType = (tupleType && i < tupleType->elems.size())
                    ? tupleType->elems[i] : nullptr;
                std::string fieldPtr = gepField(tupleType, src, i);
                if (isAggregate(fieldType))
                {
                    copyAggregate(fieldType, fieldPtr, dst);
                }
                else
                {
                    std::string v = freshTemp();
                    line(std::format("{} = load {}, ptr {}", v, llvmType(fieldType), fieldPtr));
                    line(std::format("store {} {}, ptr {}", llvmType(fieldType), v, dst));
                }
            }
            return src;
        }

        Type const* targetType = assign->target->type;
        std::string addr = emitLValueAddr(assign->target);
        if (addr.empty())
        {
            return "0";     // unsupported target, already reported
        }

        if (isAggregate(targetType))
        {
            std::string src = emitExpr(assign->value);     // a pointer
            copyAggregate(targetType, src, addr);
            return addr;
        }

        std::string ty = llvmType(targetType);

        if (assign->op == AssignOp::Assign)
        {
            std::string rhs = emitExpr(assign->value);
            line(std::format("store {} {}, ptr {}", ty, rhs, addr));
            return rhs;
        }

        std::string cur = freshTemp();
        line(std::format("{} = load {}, ptr {}", cur, ty, addr));
        std::string rhs = emitExpr(assign->value);
        std::string res = freshTemp();
        bool const flt = isFloat(targetType);
        std::string op;
        switch (assign->op)
        {
        case AssignOp::Add: op = flt ? "fadd" : "add"; break;
        case AssignOp::Sub: op = flt ? "fsub" : "sub"; break;
        case AssignOp::Mul: op = flt ? "fmul" : "mul"; break;
        case AssignOp::Div: op = flt ? "fdiv" : (isSignedKind(targetType->kind) ? "sdiv" : "udiv"); break;
        case AssignOp::Rem: op = flt ? "frem" : (isSignedKind(targetType->kind) ? "srem" : "urem"); break;
        case AssignOp::Assign: break;
        }
        line(std::format("{} = {} {} {}, {}", res, op, ty, cur, rhs));
        line(std::format("store {} {}, ptr {}", ty, res, addr));
        return res;
    }

    case ExprKind::Call:
        return emitCall(cast<CallExpr>(expr));

    case ExprKind::Cast:
        return emitCast(cast<CastExpr>(expr));

    case ExprKind::Index:
    case ExprKind::Field:
    {
        // Reading an element/field: take its address, then load a scalar (or
        // hand back the pointer if the element is itself an aggregate).
        std::string addr = emitLValueAddr(expr);
        if (addr.empty())
        {
            return "0";
        }
        if (isAggregate(expr->type))
        {
            return addr;
        }
        std::string v = freshTemp();
        line(std::format("{} = load {}, ptr {}", v, llvmType(expr->type), addr));
        return v;
    }

    case ExprKind::Array:
    {
        auto const* arr = cast<ArrayExpr>(expr);
        Type const* arrayType = arr->type;
        std::string slot = freshAlloca(llvmType(arrayType));
        for (std::size_t i = 0; i < arr->elements.size(); ++i)
        {
            std::string elemPtr = gepElement(arrayType, slot, "i64", std::to_string(i));
            std::string v = emitExpr(arr->elements[i]);
            storeInto(arrayType->elem, v, elemPtr);
        }
        return slot;
    }

    case ExprKind::Tuple:
    {
        auto const* tup = cast<TupleExpr>(expr);
        if (tup->elements.empty())
        {
            return "";          // the unit value () has no representation
        }
        Type const* tupleType = tup->type;
        std::string slot = freshAlloca(llvmType(tupleType));
        for (std::size_t i = 0; i < tup->elements.size(); ++i)
        {
            std::string fieldPtr = gepField(tupleType, slot, i);
            std::string v = emitExpr(tup->elements[i]);
            storeInto(tupleType->elems[i], v, fieldPtr);
        }
        return slot;
    }

    case ExprKind::Range:
        unsupported(expr->span, "ranges outside a for loop");
        return "0";
    }
    return "0";
}

std::string CodeGen::emitLValueAddr(Expr const* target)
{
    switch (target->kind)
    {
    case ExprKind::Name:
        return addrOf(cast<NameExpr>(target)->sym);

    case ExprKind::Index:
    {
        auto const* idx = cast<IndexExpr>(target);
        Type const* arrayType = idx->base->type;
        if (!arrayType || arrayType->kind != Type::Kind::Array)
        {
            unsupported(idx->span, "indexing this type");
            return "";
        }
        std::string base = emitExpr(idx->base);             // pointer to the array
        std::string indexTy = llvmType(idx->index->type);
        std::string index = emitExpr(idx->index);
        if (idx->fromEnd)
        {
            std::string fixed = freshTemp();                // a[^k] -> a[len - k]
            line(std::format("{} = sub {} {}, {}", fixed, indexTy, arrayType->arrayLen, index));
            index = fixed;
        }
        return gepElement(arrayType, base, indexTy, index);
    }

    case ExprKind::Field:
    {
        auto const* field = cast<FieldExpr>(target);
        Type const* tupleType = field->base->type;
        if (!tupleType || tupleType->kind != Type::Kind::Tuple)
        {
            unsupported(field->span, "field access on this type");
            return "";
        }
        std::string base = emitExpr(field->base);           // pointer to the tuple
        return gepField(tupleType, base, field->index);
    }

    default:
        unsupported(target->span, "assignment to this kind of target");
        return "";
    }
}

std::string CodeGen::emitBinary(BinaryExpr const* bin)
{
    if (bin->op == BinaryOp::And || bin->op == BinaryOp::Or)
    {
        return emitShortCircuit(bin);
    }

    std::string l = emitExpr(bin->lhs);
    std::string r = emitExpr(bin->rhs);
    Type const* opType = bin->lhs->type;        // both operands share a type
    std::string ty = llvmType(opType);
    bool const flt = isFloat(opType);
    bool const sgn = isInteger(opType) && isSignedKind(opType->kind);
    std::string t = freshTemp();

    switch (bin->op)
    {
    case BinaryOp::Add: line(std::format("{} = {} {} {}, {}", t, flt ? "fadd" : "add", ty, l, r)); break;
    case BinaryOp::Sub: line(std::format("{} = {} {} {}, {}", t, flt ? "fsub" : "sub", ty, l, r)); break;
    case BinaryOp::Mul: line(std::format("{} = {} {} {}, {}", t, flt ? "fmul" : "mul", ty, l, r)); break;
    case BinaryOp::Div: line(std::format("{} = {} {} {}, {}", t, flt ? "fdiv" : (sgn ? "sdiv" : "udiv"), ty, l, r)); break;
    case BinaryOp::Rem: line(std::format("{} = {} {} {}, {}", t, flt ? "frem" : (sgn ? "srem" : "urem"), ty, l, r)); break;

    case BinaryOp::Eq: line(std::format("{} = {} {} {} {}, {}", t, flt ? "fcmp" : "icmp", flt ? "oeq" : "eq", ty, l, r)); break;
    case BinaryOp::Ne: line(std::format("{} = {} {} {} {}, {}", t, flt ? "fcmp" : "icmp", flt ? "one" : "ne", ty, l, r)); break;
    case BinaryOp::Lt: line(std::format("{} = {} {} {} {}, {}", t, flt ? "fcmp" : "icmp", flt ? "olt" : (sgn ? "slt" : "ult"), ty, l, r)); break;
    case BinaryOp::Le: line(std::format("{} = {} {} {} {}, {}", t, flt ? "fcmp" : "icmp", flt ? "ole" : (sgn ? "sle" : "ule"), ty, l, r)); break;
    case BinaryOp::Gt: line(std::format("{} = {} {} {} {}, {}", t, flt ? "fcmp" : "icmp", flt ? "ogt" : (sgn ? "sgt" : "ugt"), ty, l, r)); break;
    case BinaryOp::Ge: line(std::format("{} = {} {} {} {}, {}", t, flt ? "fcmp" : "icmp", flt ? "oge" : (sgn ? "sge" : "uge"), ty, l, r)); break;

    case BinaryOp::And:
    case BinaryOp::Or:
        break;      // handled above
    }
    return t;
}

std::string CodeGen::emitShortCircuit(BinaryExpr const* bin)
{
    // a && b  ->  if a then b else false
    // a || b  ->  if a then true else b
    bool const isAnd = (bin->op == BinaryOp::And);
    std::string l = emitExpr(bin->lhs);
    std::string lhsBlock = freshLabel("sc.lhs");
    std::string rhsBlock = freshLabel("sc.rhs");
    std::string endBlock = freshLabel("sc.end");

    // Record the block we branch from for the phi.
    br(lhsBlock);
    label(lhsBlock);
    if (isAnd)
    {
        condBr(l, rhsBlock, endBlock);
    }
    else
    {
        condBr(l, endBlock, rhsBlock);
    }

    label(rhsBlock);
    std::string r = emitExpr(bin->rhs);
    std::string rhsEnd = freshLabel("sc.rhsend");
    br(rhsEnd);
    label(rhsEnd);
    br(endBlock);

    label(endBlock);
    std::string t = freshTemp();
    std::string shortVal = isAnd ? "false" : "true";
    line(std::format("{} = phi i1 [ {}, %{} ], [ {}, %{} ]",
                     t, shortVal, lhsBlock, r, rhsEnd));
    return t;
}

std::string CodeGen::emitCall(CallExpr const* call)
{
    auto const* callee = dyn_cast<NameExpr>(call->callee);
    Sym const* sym = callee ? callee->sym : nullptr;

    if (sym && sym->kind == SymKind::Builtin)
    {
        emitPrint(call);
        return "";
    }

    if (sym && sym->kind == SymKind::Function)
    {
        auto const* fn = cast<FnDecl>(sym->decl);
        std::string args;
        for (std::size_t i = 0; i < call->args.size(); ++i)
        {
            Type const* argType = call->args[i]->type;
            std::string argTy = llvmType(argType);
            std::string v = emitExpr(call->args[i]);
            if (isAggregate(argType))
            {
                // Pass aggregates by value: load the whole thing from its slot.
                std::string loaded = freshTemp();
                line(std::format("{} = load {}, ptr {}", loaded, argTy, v));
                v = loaded;
            }
            if (i != 0)
            {
                args += ", ";
            }
            args += std::format("{} {}", argTy, v);
        }

        std::string retIr = llvmType(call->type);
        std::string name = std::string(m_interner.lookup(callee->name));
        if (retIr == "void")
        {
            line(std::format("call void @{}({})", name, args));
            return "";
        }
        std::string t = freshTemp();
        line(std::format("{} = call {} @{}({})", t, retIr, name, args));
        if (isAggregate(call->type))
        {
            // Store the returned aggregate into a slot and hand back the pointer,
            // matching the aggregates-by-pointer model used everywhere else.
            std::string slot = freshAlloca(retIr);
            line(std::format("store {} {}, ptr {}", retIr, t, slot));
            return slot;
        }
        return t;
    }

    return "0";
}

void CodeGen::emitPrint(CallExpr const* call)
{
    if (call->args.empty())
    {
        return;
    }
    Expr const* arg = call->args[0];
    Type const* t = arg->type;
    std::string v = emitExpr(arg);

    auto printf = [&](std::string_view fmt, std::string_view argTy, std::string_view value)
    {
        line(std::format("call i32 (ptr, ...) @printf(ptr {}, {} {})", fmt, argTy, value));
    };

    if (!t)
    {
        printf("@.fmt.i32", "i32", v);
        return;
    }
    switch (t->kind)
    {
    case Type::Kind::I32: case Type::Kind::UntypedInt:
        printf("@.fmt.i32", "i32", v); break;
    case Type::Kind::U32:
        printf("@.fmt.u32", "i32", v); break;
    case Type::Kind::I64:
        printf("@.fmt.i64", "i64", v); break;
    case Type::Kind::U64:
        printf("@.fmt.u64", "i64", v); break;
    case Type::Kind::F64: case Type::Kind::UntypedFloat:
        printf("@.fmt.f64", "double", v); break;
    case Type::Kind::F32:
    {
        std::string d = freshTemp();
        line(std::format("{} = fpext float {} to double", d, v));
        printf("@.fmt.f64", "double", d);
        break;
    }
    case Type::Kind::Bool:
    {
        std::string z = freshTemp();
        line(std::format("{} = zext i1 {} to i32", z, v));
        printf("@.fmt.i32", "i32", z);
        break;
    }
    case Type::Kind::String:
        printf("@.fmt.str", "ptr", v); break;
    default:
        unsupported(arg->span, "printing this type");
        break;
    }
}

std::string CodeGen::emitCast(CastExpr const* cst)
{
    std::string v = emitExpr(cst->value);
    Type const* from = cst->value->type;
    // CastExpr::type is the target TypeExpr annotation, which shadows the
    // inherited Expr::type; the resolved semantic type lives on the base.
    Type const* to = static_cast<Expr const*>(cst)->type;
    std::string fromTy = llvmType(from);
    std::string toTy = llvmType(to);

    if (!from || !to || from == to || fromTy == toTy)
    {
        return v;       // no-op (e.g. i32 -> u32 share an LLVM type)
    }

    std::string t = freshTemp();
    std::string op;

    bool const fromInt = isInteger(from) || isUntyped(from);
    bool const toInt = isInteger(to);
    bool const fromFlt = isFloat(from);
    bool const toFlt = isFloat(to);
    bool const fromSigned = from->kind == Type::Kind::I32 || from->kind == Type::Kind::I64
        || from->kind == Type::Kind::UntypedInt;

    if (fromInt && toInt)
    {
        int fb = from->kind == Type::Kind::UntypedInt ? 32 : intBits(from->kind);
        int tb = intBits(to->kind);
        op = tb > fb ? (fromSigned ? "sext" : "zext") : "trunc";
    }
    else if (fromInt && toFlt)
    {
        op = fromSigned ? "sitofp" : "uitofp";
    }
    else if (fromFlt && toInt)
    {
        op = (to->kind == Type::Kind::I32 || to->kind == Type::Kind::I64) ? "fptosi" : "fptoui";
    }
    else if (fromFlt && toFlt)
    {
        op = (toTy == "double") ? "fpext" : "fptrunc";
    }
    else
    {
        return v;
    }

    line(std::format("{} = {} {} {} to {}", t, op, fromTy, v, toTy));
    return t;
}

} // namespace iq