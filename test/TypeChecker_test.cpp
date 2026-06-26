#include <catch2/catch_test_macros.hpp>

#include "iq/AST/AST.h"
#include "iq/AST/Arena.h"
#include "iq/AST/Casting.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Lex/Lexer.h"
#include "iq/Parse/Parser.h"
#include "iq/Sema/Resolver.h"
#include "iq/Sema/Sym.h"
#include "iq/Sema/Type.h"
#include "iq/Sema/TypeChecker.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <string>

namespace
{

// Parses, resolves, and type-checks a snippet; keeps everything the AST and the
// types point into alive for the test body.
class TestCheck
{
public:
    explicit TestCheck(std::string src)
        : m_sm("<test>", std::move(src)), m_diags(m_sm)
    {
        iq::Lexer lexer(m_sm, m_interner, m_diags);
        iq::Parser parser(lexer.tokenize(), m_sm, m_interner, m_diags, m_arena);
        m_module = parser.parseModule();

        iq::Resolver resolver(m_sm, m_interner, m_diags, m_arena);
        resolver.resolve(m_module);

        iq::TypeChecker checker(m_sm, m_interner, m_diags, m_types);
        checker.check(m_module);
    }

    iq::Module const& module() const { return m_module; }
    bool hasErrors() const { return m_diags.hasErrors(); }
    std::size_t errorCount() const { return m_diags.errorCount(); }
    iq::TypeContext& types() { return m_types; }

private:
    iq::SourceManager m_sm;
    iq::StringInterner m_interner;
    iq::DiagnosticEngine m_diags;
    iq::Arena m_arena;
    iq::TypeContext m_types;
    iq::Module m_module;
};

// Type of the initializer of the first `let` in a single function.
iq::Type const* firstLetType(TestCheck const& tc)
{
    using namespace iq;
    auto const* fn = cast<FnDecl>(tc.module().decls[0]);
    auto const* let = cast<LetStmt>(fn->body->statements[0]);
    auto const* ident = cast<IdentPattern>(let->pattern);
    return ident->sym->type;
}

} // namespace

TEST_CASE("typecheck: the sample program checks clean", "[typecheck]")
{
    TestCheck tc(R"(
        const MAX: i32 = 10;
        fn log(msg: string) { print(msg); }
        fn sum_squares(n: i32) -> i64 {
            let acc: i64 = 0;
            for i in 0..n {
                acc += (i as i64) * (i as i64);
            }
            ret acc;
        }
        fn main() {
            log("starting");
            let xs = [1, 2, 3, 4];
            let [first, ..rest, last] = xs;
            print(xs[2]);
            print(sum_squares(MAX));
        }
    )");
    REQUIRE_FALSE(tc.hasErrors());
}

TEST_CASE("typecheck: an unsuffixed int literal defaults to i32", "[typecheck]")
{
    TestCheck tc("fn main() { let a = 1; }");
    REQUIRE_FALSE(tc.hasErrors());
    REQUIRE(firstLetType(tc) == tc.types().i32());
}

TEST_CASE("typecheck: a fractional literal defaults to f64", "[typecheck]")
{
    TestCheck tc("fn main() { let a = 1.5; }");
    REQUIRE_FALSE(tc.hasErrors());
    REQUIRE(firstLetType(tc) == tc.types().f64());
}

TEST_CASE("typecheck: an annotation pins the literal", "[typecheck]")
{
    TestCheck tc("fn main() { let a: i64 = 0; }");
    REQUIRE_FALSE(tc.hasErrors());
    REQUIRE(firstLetType(tc) == tc.types().i64());
}

TEST_CASE("typecheck: a suffix conflicting with the annotation is an error", "[typecheck]")
{
    TestCheck tc("const x: i32 = 0i64;");
    REQUIRE(tc.hasErrors());
}

TEST_CASE("typecheck: mixing distinct concrete numeric types needs a cast", "[typecheck]")
{
    TestCheck tc(R"(
        fn main() {
            let a: i32 = 1;
            let b: i64 = 2;
            let c = a + b;
        }
    )");
    REQUIRE(tc.hasErrors());
}

TEST_CASE("typecheck: an explicit cast bridges numeric types", "[typecheck]")
{
    TestCheck tc(R"(
        fn main() {
            let a: i32 = 1;
            let b: i64 = 2;
            let c = (a as i64) + b;
        }
    )");
    REQUIRE_FALSE(tc.hasErrors());
}

TEST_CASE("typecheck: an if condition must be bool", "[typecheck]")
{
    TestCheck tc("fn main() { if 1 { } }");
    REQUIRE(tc.hasErrors());
}

TEST_CASE("typecheck: returning the wrong type is an error", "[typecheck]")
{
    TestCheck tc("fn f() -> i32 { ret true; }");
    REQUIRE(tc.hasErrors());
}

TEST_CASE("typecheck: a wrong-typed call argument is an error", "[typecheck]")
{
    TestCheck tc(R"(
        fn takesInt(n: i32) { print(n); }
        fn main() { takesInt(true); }
    )");
    REQUIRE(tc.hasErrors());
}

TEST_CASE("typecheck: wrong argument count is an error", "[typecheck]")
{
    TestCheck tc(R"(
        fn two(a: i32, b: i32) { print(a); }
        fn main() { two(1); }
    )");
    REQUIRE(tc.hasErrors());
}

TEST_CASE("typecheck: assigning to an immutable binding is an error", "[typecheck]")
{
    TestCheck tc("fn f(p: i32) { p = 1; }");
    REQUIRE(tc.hasErrors());
}

TEST_CASE("typecheck: indexing a non-array is an error", "[typecheck]")
{
    TestCheck tc("fn main() { let a: i32 = 1; let b = a[0]; }");
    REQUIRE(tc.hasErrors());
}

TEST_CASE("typecheck: array literal infers a fixed-length array type", "[typecheck]")
{
    using namespace iq;
    TestCheck tc("fn main() { let xs = [1, 2, 3, 4]; }");
    REQUIRE_FALSE(tc.hasErrors());
    Type const* t = firstLetType(tc);
    REQUIRE(t != nullptr);
    REQUIRE(t->kind == Type::Kind::Array);
    REQUIRE(t->arrayLen == 4);
    REQUIRE(t->elem == tc.types().i32());
}

TEST_CASE("typecheck: rest destructuring gives the slice an array type", "[typecheck]")
{
    using namespace iq;
    TestCheck tc("fn main() { let xs = [1, 2, 3, 4]; let [first, ..rest, last] = xs; }");
    REQUIRE_FALSE(tc.hasErrors());

    auto const* fn = cast<FnDecl>(tc.module().decls[0]);
    auto const* let = cast<LetStmt>(fn->body->statements[1]);
    auto const* arr = cast<ArrayPattern>(let->pattern);

    auto const* first = cast<IdentPattern>(arr->elements[0]);
    auto const* rest = cast<RestPattern>(arr->elements[1]);
    auto const* last = cast<IdentPattern>(arr->elements[2]);

    REQUIRE(first->sym->type == tc.types().i32());
    REQUIRE(last->sym->type == tc.types().i32());
    REQUIRE(rest->sym->type->kind == Type::Kind::Array);
    REQUIRE(rest->sym->type->arrayLen == 2);          // 4 - 2 fixed
    REQUIRE(rest->sym->type->elem == tc.types().i32());
}
