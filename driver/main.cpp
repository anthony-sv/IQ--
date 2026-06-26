#include "iq/AST/Arena.h"
#include "iq/AST/AstPrinter.h"
#include "iq/AST/DotPrinter.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Lex/Lexer.h"
#include "iq/Parse/Parser.h"
#include "iq/Sema/Resolver.h"
#include "iq/Sema/Type.h"
#include "iq/Sema/TypeChecker.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::string_view kSample = R"iq(
const MAX: i32 = 10;

fn log(msg: string) {
    print(msg);
}

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
)iq";

enum class Mode
{
    DumpTokens,
    DumpAstText,
    DumpAstDot,
    Check,
};

void dumpTokens(iq::SourceManager const& sm)
{
    iq::StringInterner interner;
    iq::DiagnosticEngine diags(sm);
    iq::Lexer lexer(sm, interner, diags);

    std::println("{:<5} {:<4} {:<12} {}", "line", "col", "kind", "lexeme");
    std::println("{}", std::string(48, '-'));

    for (iq::Token const& tok : lexer.tokenize())
    {
        iq::LineCol const lc = sm.lineCol(tok.span.begin);
        std::string_view lexeme = sm.spanText(tok.span);
        if (tok.kind == iq::TokenKind::Eof)
        {
            lexeme = "<eof>";
        }
        std::println("{:<5} {:<4} {:<12} {}",
                     lc.line, lc.col, iq::tokenKindName(tok.kind), lexeme);
    }

    diags.printAll();
    if (diags.hasErrors())
    {
        std::println("\n{} error(s)", diags.errorCount());
    }
}

void dumpAst(iq::SourceManager const& sm, bool asDot)
{
    iq::StringInterner interner;
    iq::DiagnosticEngine diags(sm);
    iq::Lexer lexer(sm, interner, diags);

    iq::Arena arena;
    iq::Parser parser(lexer.tokenize(), sm, interner, diags, arena);
    iq::Module const module = parser.parseModule();

    if (asDot)
    {
        // Pure DOT to stdout so it can be piped straight into `dot`. Diagnostics
        // go to stderr to keep the graph text clean.
        iq::DotPrinter printer(sm, interner);
        printer.print(module);
        diags.printAll();
        return;
    }

    iq::AstPrinter printer(sm, interner);
    printer.print(module);
    diags.printAll();
    if (diags.hasErrors())
    {
        std::println("\n{} error(s)", diags.errorCount());
    }
}

// Parse + name-resolve, reporting any diagnostics. No output on success beyond
// a short summary.
void check(iq::SourceManager const& sm)
{
    iq::StringInterner interner;
    iq::DiagnosticEngine diags(sm);
    iq::Lexer lexer(sm, interner, diags);

    iq::Arena arena;
    iq::Parser parser(lexer.tokenize(), sm, interner, diags, arena);
    iq::Module module = parser.parseModule();

    iq::Resolver resolver(sm, interner, diags, arena);
    resolver.resolve(module);

    iq::TypeContext types;
    iq::TypeChecker checker(sm, interner, diags, types);
    checker.check(module);

    diags.printAll();
    if (diags.hasErrors())
    {
        std::println("\n{} error(s)", diags.errorCount());
    }
    else
    {
        std::println("ok: {} top-level declaration(s), no errors", module.decls.size());
    }
}

void printUsage(std::string_view exe)
{
    std::println("usage: {} [--dump-tokens | --dump-ast | --dump-ast=dot | --check] [file.iq]", exe);
    std::println("  default mode is --dump-ast.");
    std::println("  --check parses, resolves names, and type-checks, reporting any errors.");
    std::println("  --dump-ast=dot emits Graphviz DOT; pipe it into the dot tool:");
    std::println("      {} --dump-ast=dot prog.iq | dot -Tpng -o prog.png", exe);
    std::println("  with no file, a built-in sample program is used.");
}

} // namespace

int main(int argc, char* argv[])
{
    std::string_view file;
    Mode mode = Mode::DumpAstText;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view const arg = argv[i];
        if (arg == "--dump-tokens")
        {
            mode = Mode::DumpTokens;
        }
        else if (arg == "--dump-ast")
        {
            mode = Mode::DumpAstText;
        }
        else if (arg == "--dump-ast=dot")
        {
            mode = Mode::DumpAstDot;
        }
        else if (arg == "--check")
        {
            mode = Mode::Check;
        }
        else if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else
        {
            file = arg;
        }
    }

    auto const run = [mode](iq::SourceManager const& sm)
    {
        switch (mode)
        {
        case Mode::DumpTokens:  dumpTokens(sm);      break;
        case Mode::DumpAstText: dumpAst(sm, false);  break;
        case Mode::DumpAstDot:  dumpAst(sm, true);   break;
        case Mode::Check:       check(sm);           break;
        }
    };

    if (!file.empty())
    {
        auto sm = iq::SourceManager::fromFile(file);
        if (!sm)
        {
            std::println(stderr, "error: cannot open '{}'", file);
            return 1;
        }
        run(*sm);
    }
    else
    {
        iq::SourceManager sm("<sample>", std::string(kSample));
        run(sm);
    }

    return 0;
}