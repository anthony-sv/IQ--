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

TEST_CASE("codegen: arrays are reported as not yet supported", "[codegen]")
{
    TestCodeGen cg("fn main() { let xs = [1, 2, 3]; }");
    REQUIRE(cg.hasErrors());
}
