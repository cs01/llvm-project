# Nullsafe Clang

**Compile-time null pointer dereference checking for C and C++.**

A fork of Clang that adds flow-sensitive nullability analysis. It catches null pointer dereferences at compile time — the same way TypeScript catches `undefined` access or Kotlin catches nullable types — but for C and C++. Opt-in, zero runtime cost, [negligible compile-time overhead](PERFORMANCE.md#real-world-benchmarks-llvmclang) — 41x faster than the Clang Static Analyzer.

> **[Try it in the online playground](https://cs01.github.io/llvm-project/)**

## Who is this for?

- You want to **prevent null pointer crashes in production** before they happen
- You work on **safety-critical or high-reliability software** (automotive, medical, aerospace, infrastructure) where a null dereference is not just a bug — it's a liability
- You're **migrating a C codebase toward modern safety guarantees** and want Kotlin/Swift-style nullability without switching languages
- You maintain a **large C/C++ codebase** and need a way to adopt null safety gradually, one file or module at a time
- You're tired of chasing **SIGSEGV crashes in CI or crash logs** that could have been caught at compile time

## The problem

Can Clang catch a null pointer dereference? Try this with every warning flag you can find:

```c
// file.c
int deref(int *p) {
    return *p;  // crashes if p is NULL
}
```

```bash
$ clang -Wall -Wextra -Wnullability -Wnull-dereference -c file.c
```

Zero warnings. OK, Clang already has `_Nullable` and `_Nonnull` annotations — let's use them:

```c
// file.c
int deref(int * _Nullable p) {
    return *p;  // crashes if p is NULL
}
```

```bash
$ clang -Wall -Wextra -Wnullability -c file.c
```

Still zero warnings. The annotation is right there. The dereference is unchecked. Clang doesn't care.

**That's why this fork exists** ([RFC on Discourse](https://discourse.llvm.org/t/rfc-flow-sensitive-nullability/89042)).

With nullsafe-clang, the same code produces a warning at compile time — no separate analysis step, no runtime cost:

```
$ nullsafe-clang -fflow-sensitive-nullability file.c

warning: dereference of nullable pointer [-Wflow-nullable-dereference]
    return *p;
            ^
note: add a null check before dereferencing, or annotate as '_Nonnull' if this pointer cannot be null
```

The warning tells you exactly what's wrong: `p` is `_Nullable`, and you're dereferencing it without checking. The fix is straightforward — add a null check, and the warning goes away:

```c
int deref(int * _Nullable p) {
    if (!p) return 0;
    return *p;  // OK — p is proven non-null
}
```

## How it compares

| | Stock Clang (`-Wnullability`) | Clang Static Analyzer | Nullsafe Clang |
|--|:-----------------------------:|:---------------------:|:--------------:|
| Analysis technique | Type checking | Symbolic execution | Dataflow on CFG |
| `_Nullable` → `_Nonnull` conversion | ✅ warns | ✅ warns | ✅ warns |
| Dereference of nullable pointer | ❌ silent | ✅ warns | ✅ warns |
| Works on unannotated code | ❌ | ❌ | ✅ |
| Runs as part of compiler | ✅ | ❌ | ✅ |
| Runs in IDE (clangd) | ✅ | ❌ | ✅ |
| Fast enough for every build | ✅ | ❌ ([41x slower on real code](PERFORMANCE.md#csa-comparison-on-real-code)) | ✅ |
| No test coverage required | ✅ | ✅ | ✅ |
| Cross-function reasoning | — | ✅ | ❌ |
| Compile-time cost | Zero | Separate pass | [0.2-8%](PERFORMANCE.md#direct-measurement-via--ftime-trace) |

Nullsafe Clang runs **inside the compiler** as a fast forward dataflow pass — same architecture as `-Wthread-safety`. It works in clangd, runs on every build, and catches bugs on unannotated code with `-fnullability-default=nullable`. On [real-world code (LLVM/Clang)](PERFORMANCE.md#real-world-benchmarks-llvmclang), the analysis accounts for 0.2-8% of compile time (median ~2%) — comparable to `-Wuninitialized`, and 41x faster than the Clang Static Analyzer. Compare all three in the **[interactive playground](https://cs01.github.io/llvm-project/)**.

ASan and UBSan are complementary but solve a different problem — they're runtime sanitizers that require test coverage, add ~2x overhead, and catch crashes *after* they happen rather than preventing them at compile time.

## Usage

```bash
# Gradual: only check annotated regions (default, zero noise on legacy code)
clang -fflow-sensitive-nullability file.c

# Defensive: treat all pointers as nullable, force null checks everywhere
clang -fflow-sensitive-nullability -fnullability-default=nullable file.c

# Treat warnings as errors
clang -fflow-sensitive-nullability -fnullability-default=nullable -Werror=flow-nullable-dereference file.c
```

### Flags

| Flag | Description |
|------|-------------|
| `-fflow-sensitive-nullability` | Enable the analysis (required) |
| `-fnullability-default=unspecified` | Default. Warnings on annotated functions and inside `#pragma assume_nonnull` regions |
| `-fnullability-default=nullable` | All unannotated pointers are nullable. Maximum checking |
| `-fnullability-default=nonnull` | All unannotated pointers are nonnull. Ergonomic mode — only annotate what *can* be null (how Kotlin and Swift work) |

### Opting in gradually

The analysis activates automatically for any function with `_Nullable` or `_Nonnull` annotations. You can also activate it for entire regions with pragmas:

```c
#pragma clang assume_nonnull begin
// unannotated pointers here are _Nonnull — annotate the nullable ones
void api_function(int* _Nullable input) {
    *input = 42;  // warning: input is _Nullable
}
#pragma clang assume_nonnull end

#pragma clang assume_nullable begin
// unannotated pointers here are _Nullable — annotate the nonnull ones
void checked_function(int* _Nonnull safe) {
    *safe = 42;  // no warning
}
#pragma clang assume_nullable end
```

You can migrate one function, one file, or one module at a time.

## Building vs. static analysis

You can use nullsafe in two ways:

- **As part of the build** — add `-fflow-sensitive-nullability` to your compiler flags and warnings show up alongside every other compile error. This is the fast path: zero extra tooling, works in clangd, catches bugs as you type.

- **As a standalone analysis step** — run with `-fsyntax-only -fnullability-default=nullable` against a compilation database (`compile_commands.json`), like a linter, without producing object files or blocking builds. This surfaces every potential null dereference in the codebase so you can fix them incrementally.

## Annotated standard library headers

Nullability-annotated `stdlib.h`, `stdio.h`, and `string.h` are included. These annotate `malloc` as returning `_Nullable`, `free` as accepting `_Nullable`, etc:

```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable \
      -I/path/to/clang/nullsafe-headers/include file.c
```

## Deeper dives

- **[Architecture Diagrams](docs/flow-nullability-architecture.md)** — Mermaid flow diagrams of the three-layer design, worklist algorithm, state tracking, and transfer functions
- **[Architecture Review Guide](docs/flow-nullability-review-guide.md)** — written walkthrough with concrete code examples for every concept
- **[Performance Benchmarks](PERFORMANCE.md)** — real-world benchmarks on LLVM/Clang (<2% overhead), synthetic stress tests, and Clang Static Analyzer comparison (41x faster)

## Limitations

- **Intraprocedural** — does not look inside called functions. Cross-function contracts are expressed with annotations on function signatures.
- **No alias tracking** — if two pointers alias the same memory, modifying one won't invalidate the other's narrowing.
- **No inferred return nullability** — annotate return types with `_Nonnull` or `_Nullable` to express return contracts.
- **Null dereferences only** — doesn't catch buffer overflows, use-after-free, or other memory bugs.

## Installation

```bash
curl -fsSL https://raw.githubusercontent.com/cs01/llvm-project/nullsafe-clang-dev/install.sh | bash
```

Or download from [releases](https://github.com/cs01/llvm-project/releases). Includes `clang` and `clangd`.

### Build from source

```bash
git clone git@github.com:cs01/llvm-project.git
cd llvm-project
git checkout nullsafe-clang-dev
cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_USE_LINKER=lld
ninja -C build clang clangd
```

## IDE integration

The fork includes `clangd`, so you get real-time warnings in your editor.

**VS Code** — install the clangd extension, then:
```json
{ "clangd.path": "/path/to/null-safe-clang/bin/clangd" }
```

**Neovim** — via lspconfig:
```lua
require('lspconfig').clangd.setup({
  cmd = { '/path/to/null-safe-clang/bin/clangd' }
})
```

## License

Same as LLVM — Apache 2.0 with LLVM Exceptions.
