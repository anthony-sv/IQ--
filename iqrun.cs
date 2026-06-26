#!/usr/bin/env dotnet
#:package ProcessX@1.5.6

// iqrun - compile an IQ-- source file to a native exe and run it.
//
//   dotnet run iqrun.cs -- demo.iq
//   dotnet run iqrun.cs -- demo.iq --keep    (also keep the .ll IR and .exe)
//
// Run from the repository root (where driver\x64\Debug\IQ--.exe lives).
// Build the compiler first: open IQ--.slnx in Visual Studio (Debug|x64), or
//   msbuild driver\IQ--.vcxproj /p:Configuration=Debug /p:Platform=x64

using Zx;
using static Zx.Env;
using Cysharp.Diagnostics;

if (args.Length == 0)
{
    log("usage: dotnet run iqrun.cs -- <source.iq> [--keep]", ConsoleColor.Yellow);
    return 1;
}

var source = Path.GetFullPath(args[0]);
var keep = args.Contains("--keep");

if (!File.Exists(source))
{
    log($"no such file: {source}", ConsoleColor.Red);
    return 1;
}

var root = Directory.GetCurrentDirectory();
var iq = Path.Combine(root, "driver", "x64", "Debug", "IQ--.exe");
if (!File.Exists(iq))
{
    log($"compiler not built: {iq}", ConsoleColor.Red);
    log("Build IQ--.slnx (Debug|x64) first, and run this from the repo root.", ConsoleColor.Yellow);
    return 1;
}

// Prefer the clang bundled with Visual Studio; fall back to one on PATH.
var clang = @"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang.exe";
if (!File.Exists(clang)) clang = "clang";

var name = Path.GetFileNameWithoutExtension(source);
var ll = Path.Combine(root, name + ".ll");
var exe = Path.Combine(root, name + ".exe");

// 1. IQ-- source -> LLVM IR, written straight to a file by the compiler (-o),
//    so there is no stdout to echo. On a compile error the process exits non-zero
//    and ProcessX throws; we surface the diagnostics it wrote to stderr.
try
{
    await run($"{iq} --emit-llvm {source} -o {ll}");
}
catch (ProcessErrorException ex)
{
    log($"--- {name} failed to compile ---", ConsoleColor.Red);
    foreach (var line in ex.ErrorOutput) Console.Error.WriteLine(line);
    return ex.ExitCode == 0 ? 1 : ex.ExitCode;
}

// 2. LLVM IR -> native executable. run($"") auto-quotes the interpolated holes,
//    so paths with spaces are safe; the override-triple warning is silenced.
await run($"{clang} {ll} -o {exe} -Wno-override-module");

// 3. Run it.
log($"--- running {name} ---", ConsoleColor.Cyan);
await run($"{exe}");

// 4. Clean up intermediates unless asked to keep them.
if (!keep)
{
    File.Delete(ll);
    File.Delete(exe);
}
return 0;