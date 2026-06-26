#include "iq/AST/Arena.h"
#include "iq/AST/AstPrinter.h"
#include "iq/AST/DotPrinter.h"
#include "iq/CodeGen/CodeGen.h"
#include "iq/Diag/Diagnostic.h"
#include "iq/Lex/Lexer.h"
#include "iq/Parse/Parser.h"
#include "iq/Sema/Resolver.h"
#include "iq/Sema/Type.h"
#include "iq/Sema/TypeChecker.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <fstream>
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
    EmitLlvm,
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

// Run the full pipeline and emit LLVM IR. With an output path the IR is written
// to that file; otherwise it goes to stdout (diagnostics always go to stderr).
// Emits nothing if the program does not check.
int emitLlvm(iq::SourceManager const& sm, std::string_view outputPath)
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

    iq::CodeGen codegen(sm, interner, diags, types);
    std::string ir = codegen.run(module);

    if (diags.hasErrors())
    {
        diags.printAll();
        std::println(stderr, "\n{} error(s); no output produced", diags.errorCount());
        return 1;
    }

    if (!outputPath.empty())
    {
        std::ofstream out(std::string{ outputPath }, std::ios::binary);
        if (!out)
        {
            std::println(stderr, "error: cannot write '{}'", outputPath);
            return 1;
        }
        out << ir;
    }
    else
    {
        std::print("{}", ir);
    }
    return 0;
}

void printUsage(std::string_view exe)
{
    std::println("usage: {} [--dump-tokens | --dump-ast | --dump-ast=dot | --check | --emit-llvm] [file.iq]", exe);
    std::println("  default mode is --dump-ast.");
    std::println("  --check parses, resolves names, and type-checks, reporting any errors.");
    std::println("  --emit-llvm writes LLVM IR (to stdout, or to a file with -o):");
    std::println("      {} --emit-llvm prog.iq -o prog.ll && clang prog.ll -o prog.exe", exe);
    std::println("  --dump-ast=dot emits Graphviz DOT; pipe it into the dot tool:");
    std::println("      {} --dump-ast=dot prog.iq | dot -Tpng -o prog.png", exe);
    std::println("  with no file, a built-in sample program is used.");
}

} // namespace

int main(int argc, char* argv[])
{
    std::string_view file;
    std::string_view outputPath;
    Mode mode = Mode::DumpAstText;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view const arg = argv[i];
        if (arg == "-o")
        {
            if (i + 1 < argc)
            {
                outputPath = argv[++i];
            }
        }
        else if (arg == "--dump-tokens")
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
        else if (arg == "--emit-llvm")
        {
            mode = Mode::EmitLlvm;
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

    auto const run = [mode, outputPath](iq::SourceManager const& sm) -> int
    {
        switch (mode)
        {
        case Mode::DumpTokens:  dumpTokens(sm);      return 0;
        case Mode::DumpAstText: dumpAst(sm, false);  return 0;
        case Mode::DumpAstDot:  dumpAst(sm, true);   return 0;
        case Mode::Check:       check(sm);           return 0;
        case Mode::EmitLlvm:    return emitLlvm(sm, outputPath);
        }
        return 0;
    };

    if (!file.empty())
    {
        auto sm = iq::SourceManager::fromFile(file);
        if (!sm)
        {
            std::println(stderr, "error: cannot open '{}'", file);
            return 1;
        }
        return run(*sm);
    }

    iq::SourceManager sm("<sample>", std::string(kSample));
    return run(sm);
}