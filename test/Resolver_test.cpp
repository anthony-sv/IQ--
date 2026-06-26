#include <catch2/catch_test_macros.hpp>

#include "iq/AST/AST.h"
#include "iq/AST/Arena.h"
#include "iq/AST/Casting.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Lex/Lexer.h"
#include "iq/Parse/Parser.h"
#include "iq/Sema/Resolver.h"
#include "iq/Sema/Sym.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <string>

namespace
{

// Parses and name-resolves a snippet, keeping everything the AST points into
// alive for the test body.
class TestResolve
{
public:
    explicit TestResolve(std::string src)
        : m_sm("<test>", std::move(src)), m_diags(m_sm)
    {
        iq::Lexer lexer(m_sm, m_interner, m_diags);
        iq::Parser parser(lexer.tokenize(), m_sm, m_interner, m_diags, m_arena);
        m_module = parser.parseModule();

        iq::Resolver resolver(m_sm, m_interner, m_diags, m_arena);
        resolver.resolve(m_module);
    }

    iq::Module const& module() const { return m_module; }
    bool hasErrors() const { return m_diags.hasErrors(); }
    std::size_t errorCount() const { return m_diags.errorCount(); }

private:
    iq::SourceManager m_sm;
    iq::StringInterner m_interner;
    iq::DiagnosticEngine m_diags;
    iq::Arena m_arena;
    iq::Module m_module;
};

// Find the first NameExpr anywhere under an expression (depth-first), or null.
iq::NameExpr const* firstName(iq::Expr const* expr);

iq::NameExpr const* firstNameInStmt(iq::Stmt const* stmt)
{
    using namespace iq;
    if (auto const* es = dyn_cast<ExprStmt>(stmt))
    {
        return firstName(es->expr);
    }
    if (auto const* ret = dyn_cast<ReturnStmt>(stmt))
    {
        return ret->value ? firstName(ret->value) : nullptr;
    }
    return nullptr;
}

iq::NameExpr const* firstName(iq::Expr const* expr)
{
    using namespace iq;
    switch (expr->kind)
    {
    case ExprKind::Name:   return cast<NameExpr>(expr);
    case ExprKind::Unary:  return firstName(cast<UnaryExpr>(expr)->operand);
    case ExprKind::Binary: return firstName(cast<BinaryExpr>(expr)->lhs);
    case ExprKind::Assign: return firstName(cast<AssignExpr>(expr)->target);
    case ExprKind::Call:   return firstName(cast<CallExpr>(expr)->callee);
    case ExprKind::Index:  return firstName(cast<IndexExpr>(expr)->base);
    case ExprKind::Cast:   return firstName(cast<CastExpr>(expr)->value);
    default:               return nullptr;
    }
}

} // namespace

TEST_CASE("resolver: a well-formed program resolves cleanly", "[resolver]")
{
    TestResolve r(R"(
        const MAX: i32 = 10;
        fn helper(n: i32) -> i32 { ret n; }
        fn main() {
            let x = helper(MAX);
            print(x);
        }
    )");
    REQUIRE_FALSE(r.hasErrors());
}

TEST_CASE("resolver: undeclared name is an error", "[resolver]")
{
    TestResolve r("fn main() { let y = x; }");
    REQUIRE(r.hasErrors());
}

TEST_CASE("resolver: redefinition in the same scope is an error", "[resolver]")
{
    TestResolve r("fn main() { let a = 1; let a = 2; }");
    REQUIRE(r.errorCount() == 1);
}

TEST_CASE("resolver: shadowing in an inner scope is allowed", "[resolver]")
{
    TestResolve r("fn main() { let a = 1; { let a = 2; print(a); } }");
    REQUIRE_FALSE(r.hasErrors());
}

TEST_CASE("resolver: functions may be referenced before they are defined", "[resolver]")
{
    TestResolve r(R"(
        fn main() { print(later()); }
        fn later() -> i32 { ret 1; }
    )");
    REQUIRE_FALSE(r.hasErrors());
}

TEST_CASE("resolver: a local used before its let is undeclared", "[resolver]")
{
    TestResolve r("fn main() { print(x); let x = 1; }");
    REQUIRE(r.hasErrors());
}

TEST_CASE("resolver: break outside a loop is an error", "[resolver]")
{
    TestResolve r("fn main() { break; }");
    REQUIRE(r.hasErrors());
}

TEST_CASE("resolver: break inside a loop is fine", "[resolver]")
{
    TestResolve r("fn main() { for i in 0..3 { break; } }");
    REQUIRE_FALSE(r.hasErrors());
}

TEST_CASE("resolver: a name binds to the right kind of symbol", "[resolver]")
{
    using namespace iq;
    TestResolve r(R"(
        const MAX: i32 = 10;
        fn main() { print(MAX); }
    )");
    REQUIRE_FALSE(r.hasErrors());

    auto const* main = cast<FnDecl>(r.module().decls[1]);
    auto const* call = firstNameInStmt(main->body->statements[0]); // 'print'
    REQUIRE(call != nullptr);
    REQUIRE(call->sym != nullptr);
    REQUIRE(call->sym->kind == SymKind::Builtin);

    // The argument MAX resolves to the global const.
    auto const* es = cast<ExprStmt>(main->body->statements[0]);
    auto const* callExpr = cast<CallExpr>(es->expr);
    auto const* arg = cast<NameExpr>(callExpr->args[0]);
    REQUIRE(arg->sym != nullptr);
    REQUIRE(arg->sym->kind == SymKind::GlobalConst);
}
