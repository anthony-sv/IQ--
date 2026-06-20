#include "iq/Diag/Diagnostic.h"
#include "iq/Lex/Lexer.h"
#include "iq/Source/SourceManager.h"
#include "iq/Support/StringInterner.h"

#include <print>
#include <string>
#include <string_view>

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

void printUsage(std::string_view exe)
{
    std::println("usage: {} [--dump-tokens] [file.iq]", exe);
    std::println("  with no file, a built-in sample program is used.");
}

} // namespace

int main(int argc, char* argv[])
{
    std::string_view file;
    bool dumpTokensMode = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view const arg = argv[i];
        if (arg == "--dump-tokens")
        {
            dumpTokensMode = true;
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

    dumpTokensMode = true;

    if (!file.empty())
    {
        auto sm = iq::SourceManager::fromFile(file);
        if (!sm)
        {
            std::println(stderr, "error: cannot open '{}'", file);
            return 1;
        }
        dumpTokens(*sm);
    }
    else
    {
        iq::SourceManager sm("<sample>", std::string(kSample));
        dumpTokens(sm);
    }

    return 0;
}