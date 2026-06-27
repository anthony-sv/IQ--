#include <catch2/catch_test_macros.hpp>

#include "iq/AST/AST.h"
#include "iq/AST/Arena.h"
#include "iq/AST/Casting.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Lex/Lexer.h"
#include "iq/Parse/Parser.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <string>
#include <string_view>

namespace
{

// Owns everything the AST transitively points into (arena memory, interned
// symbols, source text), so the parsed Module stays valid for the test body.
class TestParse
{
public:
    explicit TestParse(std::string src)
        : m_sm("<test>", std::move(src)), m_diags(m_sm)
    {
        iq::Lexer lexer(m_sm, m_interner, m_diags);
        iq::Parser parser(lexer.tokenize(), m_sm, m_interner, m_diags, m_arena);
        m_module = parser.parseModule();
    }

    iq::Module const& module() const { return m_module; }
    iq::DiagnosticEngine const& diags() const { return m_diags; }
    std::string_view name(iq::Symbol s) const { return m_interner.lookup(s); }

private:
    iq::SourceManager m_sm;
    iq::StringInterner m_interner;
    iq::DiagnosticEngine m_diags;
    iq::Arena m_arena;
    iq::Module m_module;
};

} // namespace

TEST_CASE("parser: empty function", "[parser]")
{
    TestParse p("fn main() {}");
    REQUIRE_FALSE(p.diags().hasErrors());
    REQUIRE(p.module().decls.size() == 1);

    auto const* fn = iq::dyn_cast<iq::FnDecl>(p.module().decls[0]);
    REQUIRE(fn != nullptr);
    REQUIRE(p.name(fn->name) == "main");
    REQUIRE(fn->params.empty());
    REQUIRE(fn->returnType == nullptr);          // omitted -> void
    REQUIRE(fn->body->statements.empty());
}

TEST_CASE("parser: arithmetic precedence (* binds tighter than +)", "[parser]")
{
    TestParse p("const c = 1 + 2 * 3;");
    auto const* c = iq::dyn_cast<iq::ConstDecl>(p.module().decls[0]);
    REQUIRE(c != nullptr);

    auto const* add = iq::dyn_cast<iq::BinaryExpr>(c->init);
    REQUIRE(add != nullptr);
    REQUIRE(add->op == iq::BinaryOp::Add);
    REQUIRE(iq::isa<iq::NumberLiteral>(add->lhs));

    auto const* mul = iq::dyn_cast<iq::BinaryExpr>(add->rhs);
    REQUIRE(mul != nullptr);
    REQUIRE(mul->op == iq::BinaryOp::Mul);
}

TEST_CASE("parser: assignment is right-associative", "[parser]")
{
    TestParse p("fn f() { x = y = z; }");
    auto const* fn = iq::cast<iq::FnDecl>(p.module().decls[0]);
    auto const* stmt = iq::dyn_cast<iq::ExprStmt>(fn->body->statements[0]);
    REQUIRE(stmt != nullptr);

    auto const* outer = iq::dyn_cast<iq::AssignExpr>(stmt->expr);
    REQUIRE(outer != nullptr);
    REQUIRE(iq::isa<iq::NameExpr>(outer->target));       // x
    REQUIRE(iq::isa<iq::AssignExpr>(outer->value));      // (y = z)
}

TEST_CASE("parser: cast binds tighter than multiply", "[parser]")
{
    TestParse p("const c = a as i64 * b;");
    auto const* c = iq::cast<iq::ConstDecl>(p.module().decls[0]);

    auto const* mul = iq::dyn_cast<iq::BinaryExpr>(c->init);
    REQUIRE(mul != nullptr);
    REQUIRE(mul->op == iq::BinaryOp::Mul);
    REQUIRE(iq::isa<iq::CastExpr>(mul->lhs));            // (a as i64)
    REQUIRE(iq::isa<iq::NameExpr>(mul->rhs));            // b
}

TEST_CASE("parser: array destructuring with bindable rest", "[parser]")
{
    TestParse p("fn f() { let [a, ..rest, b] = xs; }");
    auto const* fn = iq::cast<iq::FnDecl>(p.module().decls[0]);
    auto const* let = iq::dyn_cast<iq::LetStmt>(fn->body->statements[0]);
    REQUIRE(let != nullptr);

    auto const* arr = iq::dyn_cast<iq::ArrayPattern>(let->pattern);
    REQUIRE(arr != nullptr);
    REQUIRE(arr->elements.size() == 3);
    REQUIRE(iq::isa<iq::IdentPattern>(arr->elements[0]));

    auto const* rest = iq::dyn_cast<iq::RestPattern>(arr->elements[1]);
    REQUIRE(rest != nullptr);
    REQUIRE(rest->binds());
    REQUIRE(p.name(rest->name) == "rest");

    REQUIRE(iq::isa<iq::IdentPattern>(arr->elements[2]));
}

TEST_CASE("parser: tuple field access parses to a Field expr", "[parser]")
{
    using namespace iq;
    TestParse p("fn main() { let x = t.0; }");
    auto const* fn = cast<FnDecl>(p.module().decls[0]);
    auto const* let = cast<LetStmt>(fn->body->statements[0]);
    auto const* field = dyn_cast<FieldExpr>(let->init);
    REQUIRE(field != nullptr);
    REQUIRE(field->index == 0);
    REQUIRE(iq::isa<NameExpr>(field->base));
}

TEST_CASE("parser: chained tuple fields split a merged number token", "[parser]")
{
    using namespace iq;
    TestParse p("fn main() { let x = t.0.1; }");      // lexes t '.' 0.1
    auto const* fn = cast<FnDecl>(p.module().decls[0]);
    auto const* let = cast<LetStmt>(fn->body->statements[0]);
    auto const* outer = dyn_cast<FieldExpr>(let->init);
    REQUIRE(outer != nullptr);
    REQUIRE(outer->index == 1);
    auto const* inner = dyn_cast<FieldExpr>(outer->base);
    REQUIRE(inner != nullptr);
    REQUIRE(inner->index == 0);
}

TEST_CASE("parser: recovers after a broken declaration", "[parser]")
{
    TestParse p("fn broken( {}\nfn good() {}");
    REQUIRE(p.diags().hasErrors());

    bool foundGood = false;
    for (iq::Decl const* d : p.module().decls)
    {
        if (auto const* fn = iq::dyn_cast<iq::FnDecl>(d))
        {
            if (p.name(fn->name) == "good")
            {
                foundGood = true;
            }
        }
    }
    REQUIRE(foundGood);
}
