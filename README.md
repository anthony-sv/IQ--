<div align="center">

# IQ--

**A tiny, statically-typed language that compiles to native code through LLVM — written from scratch, by hand, to learn how real compilers work.**

![Language](https://img.shields.io/badge/written%20in-C%2B%2B23-00599C)
![Backend](https://img.shields.io/badge/backend-LLVM%20IR-orange)
![Frontend](https://img.shields.io/badge/frontend-hand--written-success)
![Tests](https://img.shields.io/badge/tests-121%20passing-brightgreen)
![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)
![License](https://img.shields.io/badge/license-Apache%202.0-blue)

</div>

---

IQ-- is a complete (if small) compiler: a **hand-written lexer and recursive-descent / Pratt parser**, a **name resolver**, a **bidirectional type checker**, and a **backend that emits textual LLVM IR** — which `clang` turns into a real native executable. The compiler core has **zero third-party dependencies** (pure C++23 standard library); the only dependency anywhere is [Catch2](https://github.com/catchorg/Catch2) for the test suite.

It is an *educational* project — every stage is built to be read and understood, using the same techniques production compilers (Clang, rustc) use: 32-bit source offsets, table-driven character classification, string interning, LLVM-style RTTI on the AST, panic-mode error recovery with caret diagnostics, an arena allocator, canonical interned types, and bidirectional inference.

```rust
fn sum_sq(n: i32) -> i64 {
    let acc: i64 = 0;
    for i in 0..n { acc += (i as i64) * (i as i64); }
    ret acc;
}

fn main() {
    let xs = [1, 2, 3, 4];
    let [first, ..rest, last] = xs;   // destructuring with a bindable rest
    let pair = (first, last);         // tuples
    print(pair.0 + pair.1);           // 5
    print(xs[^1]);                    // 4   (index from the end)
    print(sum_sq(10));                // 285
}
```

```console
$ IQ--.exe --run program.iq
5
4
285
```

## The pipeline

```
 .iq source
     │
     ▼
 ┌────────┐   ┌────────┐   ┌──────────┐   ┌──────────────┐   ┌──────────┐
 │ Lexer  │──▶│ Parser │──▶│ Resolver │──▶│ Type checker │──▶│ CodeGen  │
 │ tokens │   │  AST   │   │  scopes  │   │ types + sema │   │ LLVM IR  │
 └────────┘   └────────┘   └──────────┘   └──────────────┘   └────┬─────┘
                                                                  │  .ll
                                                                  ▼
                                                          clang ─▶ native .exe
```

## Quick start

> Requires Visual Studio 2026 (MSVC v145, C++23). `clang` ships with Visual Studio, so no separate LLVM install is needed.

```powershell
# 1. Build (Visual Studio: open IQ--.slnx and Build, or from a dev shell)
msbuild driver\IQ--.vcxproj /p:Configuration=Debug /p:Platform=x64

# 2. Compile & run an IQ-- program in one step
driver\x64\Debug\IQ--.exe --run program.iq
```

Every compiler stage is also inspectable on its own:

| Mode | What it does |
|------|--------------|
| `--dump-tokens` | print the token stream from the lexer |
| `--dump-ast` | print the AST as an indented tree |
| `--dump-ast=dot` | emit the AST as Graphviz DOT (`… \| dot -Tpng -o ast.png`) |
| `--check` | parse + resolve + type-check, report errors only |
| `--emit-llvm [-o f.ll]` | emit LLVM IR (to stdout or a file) |
| `--run` | compile via `clang` and execute |

## What the language supports today

**Types** — `i32 i64 u32 u64 f32 f64`, `bool`, `string`, `void`, fixed arrays `[T; N]`, and n-tuples `(A, B, C, …)`.

**Declarations & flow** — `fn` (with a `void` default return), top-level `const`, `let`/`const` locals, `if/else`, `while`, `for x in lo..hi` (`..=` inclusive), `ret`, `break`, `continue`.

**Expressions** — arithmetic, comparisons, short-circuit `&& ||`, unary `- !`, assignment and compound `+= -= *= /= %=`, calls, array indexing `a[i]` and from-end `a[^k]`, tuple field access `t.0`, `as` casts, array/tuple literals, and `print`.

**Compile-time guarantees** — name resolution (undeclared / redefined / use-before-declaration), **bidirectional type inference** (untyped literals are pinned by context; **no implicit numeric widening** — use `as`), suffix-vs-annotation conflict detection, **all-paths-return** analysis (it knows a non-breaking `while true` diverges), const-expression array lengths, and mutability checks.

## Algorithms & data structures you can write *right now*

Every snippet below **compiles and runs** on IQ-- as it stands today.

<table>
<tr><th>Recursion — Fibonacci</th><th>Euclid's GCD</th></tr>
<tr>
<td>

```rust
fn fib(n: i32) -> i64 {
    if n < 2 { ret n as i64; }
    ret fib(n - 1) + fib(n - 2);
}
// fib(10) -> 55
```

</td>
<td>

```rust
fn gcd(a: i32, b: i32) -> i32 {
    let x = a; let y = b;
    while y != 0 {
        let t = y;
        y = x % y;
        x = t;
    }
    ret x;
}
// gcd(48, 36) -> 12
```

</td>
</tr>
<tr><th>Primality test</th><th>Binary search (read-only array)</th></tr>
<tr>
<td>

```rust
fn is_prime(n: i32) -> bool {
    if n < 2 { ret false; }
    let i = 2;
    while i * i <= n {
        if n % i == 0 { ret false; }
        i += 1;
    }
    ret true;
}
// is_prime(17) -> true
```

</td>
<td>

```rust
let xs = [1, 3, 5, 7, 9, 11];
let lo = 0; let hi = 5;
let found = -1;
while lo <= hi {
    let mid = (lo + hi) / 2;
    if xs[mid] == 7 { found = mid; break; }
    if xs[mid] < 7 { lo = mid + 1; }
    else { hi = mid - 1; }
}
// found -> 3
```

</td>
</tr>
</table>

### Problem coverage vs. mainstream languages

How far the language reaches today, problem by problem. The mainstream languages can of course do all of these — the point is to show IQ--'s current frontier.

| Problem / data structure | IQ-- | Rust | C++ | C# | Java | Python |
|---|:--:|:--:|:--:|:--:|:--:|:--:|
| Recursion (Fibonacci, factorial) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Euclid's GCD, fast exponentiation | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Primality / sieve over a fixed array | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Sum / min / max / count over an array | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Linear & binary search (read-only) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Fixed-size tuple records (e.g. a point) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| In-place sorting (bubble / insertion) | ⚠️ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Dynamic array / stack / queue | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Hash map / set | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Linked list / tree / graph | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| String processing (length, slicing) | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |

✅ supported &nbsp;·&nbsp; ⚠️ *array element assignment isn't lowered yet, so the algorithm type-checks but won't codegen* &nbsp;·&nbsp; ❌ needs language features not built yet (heap, `struct`, generics).

### Feature comparison

| Feature | IQ-- | Rust | C++ | C# | Java | Python |
|---|:--:|:--:|:--:|:--:|:--:|:--:|
| Static type checking | ✅ | ✅ | ✅ | ✅ | ✅ | ⚠️ hints |
| Local type inference | ✅ | ✅ | ✅ `auto` | ✅ `var` | ⚠️ `var` | n/a |
| Bidirectional literal inference | ✅ | ✅ | ❌ | ❌ | ❌ | n/a |
| Sized integer types (`i32`/`u64`…) | ✅ | ✅ | ✅ | ✅ | ⚠️ no unsigned | ❌ bignum |
| No implicit numeric widening | ✅ | ✅ | ❌ | ❌ | ❌ | n/a |
| Tuples (n-ary) | ✅ | ✅ | ✅ | ✅ | ❌ | ✅ |
| Pattern destructuring (`let [a,..b,c]`) | ✅ | ✅ | ⚠️ | ⚠️ | ⚠️ | ✅ |
| Fixed-size arrays | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Dynamic collections / generics | ❌ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Compiles to native code | ✅ LLVM | ✅ | ✅ | ⚠️ JIT/AOT | ⚠️ JVM | ❌ |

### The same program, three ways

Euclid's GCD — IQ-- sits comfortably in the C / Rust family:

<table>
<tr><td><b>IQ--</b></td><td><b>Rust</b></td><td><b>Python</b></td></tr>
<tr><td>

```rust
fn gcd(a: i32, b: i32)
        -> i32 {
    let x = a; let y = b;
    while y != 0 {
        let t = y;
        y = x % y;
        x = t;
    }
    ret x;
}
```

</td><td>

```rust
fn gcd(a: i32, b: i32)
        -> i32 {
    let (mut x, mut y) =
        (a, b);
    while y != 0 {
        let t = y;
        y = x % y;
        x = t;
    }
    x
}
```

</td><td>

```python
def gcd(a, b):
    x, y = a, b
    while y != 0:
        x, y = y, x % y
    return x
```

</td></tr>
</table>

## What's *not* supported yet

Being honest about the frontier:

- **Aggregates across function boundaries** — you can build, index, field-access, and destructure arrays/tuples *inside* a function, but passing one as an argument or returning one isn't lowered yet.
- **Array element assignment** (`a[i] = x`) — type-checks but isn't lowered, so in-place sorting doesn't run yet.
- **No heap** — no `Vec`, `String` operations, maps, or linked structures.
- **No `struct`, `match`, generics, or modules** — the pattern grammar and the tuple/field machinery are already in place to make `struct` and `match` the next natural additions.

## Project layout

```
IQ--/
├── frontend/    IQFrontend.vcxproj  — lexer, parser, AST, resolver, type checker
├── backend/     IQBackend.vcxproj   — LLVM IR code generation
├── driver/      IQ--.vcxproj        — the command-line compiler (IQ--.exe)
├── test/        IQTests.vcxproj     — Catch2 suite (55 cases, 121 assertions)
└── iqrun.cs                         — optional .NET 10 helper (ProcessX / Zx)
```

Headers live under `include/iq/…`, implementations under `src/…`, mirroring Clang's layout.

## Building & running the tests

```powershell
msbuild test\IQTests.vcxproj /p:Configuration=Debug /p:Platform=x64
test\x64\Debug\IQTests.exe
```

## License

[Apache License 2.0](LICENSE.txt).

<div align="center">
<sub>Built as a hands-on study of modern compiler construction.</sub>
</div>
