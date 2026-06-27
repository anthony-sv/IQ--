#include <catch2/catch_test_macros.hpp>

#include "iq/AST/AST.h"
#include "iq/AST/Arena.h"
#include "iq/CodeGen/CodeGen.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Lex/Lexer.h"
#include "iq/Parse/Parser.h"
#include "iq/Sema/Resolver.h"
#include "iq/Sema/Type.h"
#include "iq/Sema/TypeChecker.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <string>

namespace
{

// Runs the whole pipeline and exposes the emitted LLVM IR.
class TestCodeGen
{
public:
    explicit TestCodeGen(std::string src)
        : m_sm("<test>", std::move(src)), m_diags(m_sm)
    {
        iq::Lexer lexer(m_sm, m_interner, m_diags);
        iq::Parser parser(lexer.tokenize(), m_sm, m_interner, m_diags, m_arena);
        iq::Module module = parser.parseModule();

        iq::Resolver resolver(m_sm, m_interner, m_diags, m_arena);
        resolver.resolve(module);

        iq::TypeChecker checker(m_sm, m_interner, m_diags, m_types);
        checker.check(module);

        iq::CodeGen codegen(m_sm, m_interner, m_diags, m_types);
        m_ir = codegen.run(module);
    }

    std::string const& ir() const { return m_ir; }
    bool hasErrors() const { return m_diags.hasErrors(); }
    bool contains(std::string_view needle) const
    {
        return m_ir.find(needle) != std::string::npos;
    }

private:
    iq::SourceManager m_sm;
    iq::StringInterner m_interner;
    iq::DiagnosticEngine m_diags;
    iq::Arena m_arena;
    iq::TypeContext m_types;
    std::string m_ir;
};

} // namespace

TEST_CASE("codegen: a function call lowers to LLVM call/add", "[codegen]")
{
    TestCodeGen cg(R"(
        fn add(a: i32, b: i32) -> i32 { ret a + b; }
        fn main() { print(add(3, 4)); }
    )");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("define i32 @add(i32 %a, i32 %b)"));
    REQUIRE(cg.contains("add i32"));
    REQUIRE(cg.contains("call i32 @add(i32 3, i32 4)"));
    REQUIRE(cg.contains("define i32 @main()"));
}

TEST_CASE("codegen: a for loop lowers to a comparison-driven loop", "[codegen]")
{
    TestCodeGen cg(R"(
        fn main() {
            let acc = 0;
            for i in 0..10 { acc += i; }
            print(acc);
        }
    )");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("icmp slt i32"));
    REQUIRE(cg.contains("br label"));
}

TEST_CASE("codegen: print(i32) lowers to a printf call", "[codegen]")
{
    TestCodeGen cg("fn main() { print(42); }");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("@printf"));
    REQUIRE(cg.contains("@.fmt.i32"));
}

TEST_CASE("codegen: a cast lowers to the right conversion op", "[codegen]")
{
    TestCodeGen cg(R"(
        fn main() {
            let a: i32 = 3;
            let b = a as i64;
            print(b);
        }
    )");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("sext i32"));
}

TEST_CASE("codegen: an array literal lowers to a fixed-size alloca + stores", "[codegen]")
{
    TestCodeGen cg("fn main() { let xs = [1, 2, 3, 4]; print(xs[2]); }");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("alloca [4 x i32]"));
    REQUIRE(cg.contains("getelementptr [4 x i32]"));
}

TEST_CASE("codegen: rest destructuring copies a contiguous slice", "[codegen]")
{
    TestCodeGen cg("fn main() { let xs = [1, 2, 3, 4]; let [first, ..rest, last] = xs; }");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("alloca [2 x i32]"));   // rest : [i32; 2]
    REQUIRE(cg.contains("load [2 x i32]"));     // the slice copy
}

TEST_CASE("codegen: from-end index computes len - k", "[codegen]")
{
    TestCodeGen cg("fn main() { let xs = [1, 2, 3]; print(xs[^1]); }");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("sub i32 3,"));         // len - k
}

TEST_CASE("codegen: tuple field access lowers to a struct GEP", "[codegen]")
{
    TestCodeGen cg("fn main() { let p = (10, 20); print(p.1); }");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("getelementptr {"));
}

TEST_CASE("codegen: array element assignment lowers to a GEP + store", "[codegen]")
{
    TestCodeGen cg("fn main() { let xs = [1, 2, 3]; xs[0] = 9; print(xs[0]); }");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("getelementptr [3 x i32]"));
    REQUIRE(cg.contains("store i32 9"));
}

TEST_CASE("codegen: tuple field assignment lowers to a struct GEP + store", "[codegen]")
{
    TestCodeGen cg("fn main() { let p = (1, 2); p.1 = 7; print(p.1); }");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("getelementptr {"));
    REQUIRE(cg.contains("store i32 7"));
}

TEST_CASE("codegen: parallel assignment lowers without error", "[codegen]")
{
    TestCodeGen cg("fn main() { let xs = [1, 2, 3]; (xs[0], xs[2]) = (xs[2], xs[0]); print(xs[0]); }");
    REQUIRE_FALSE(cg.hasErrors());
}

TEST_CASE("codegen: a function can return a tuple (multiple values)", "[codegen]")
{
    TestCodeGen cg(R"(
        fn divmod(a: i32, b: i32) -> (i32, i32) { ret (a / b, a % b); }
        fn main() { let (q, r) = divmod(17, 5); print(q); print(r); }
    )");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("define { i32, i32 } @divmod"));
}

TEST_CASE("codegen: an array can be passed to and returned from a function", "[codegen]")
{
    TestCodeGen cg(R"(
        fn sum4(xs: [i32; 4]) -> i32 {
            let t = 0;
            for i in 0..4 { t += xs[i]; }
            ret t;
        }
        fn make() -> [i32; 3] { ret [1, 2, 3]; }
        fn main() { let xs = [1, 2, 3, 4]; print(sum4(xs)); let ys = make(); print(ys[0]); }
    )");
    REQUIRE_FALSE(cg.hasErrors());
    REQUIRE(cg.contains("define i32 @sum4([4 x i32]"));
    REQUIRE(cg.contains("define [3 x i32] @make"));
}
