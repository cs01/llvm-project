# Nullsafe Clang

**Compile-time null pointer dereference checking for C and C++.**

[![Test Null-Safety](https://github.com/cs01/llvm-project/actions/workflows/test-null-safety.yml/badge.svg)](https://github.com/cs01/llvm-project/actions/workflows/test-null-safety.yml) &nbsp; [![Try Online](https://img.shields.io/badge/try-online-brightgreen)](https://cs01.github.io/llvm-project/)

A fork of Clang that adds flow-sensitive nullability analysis. It catches null pointer dereferences at compile time — the same way TypeScript catches `undefined` access or Kotlin catches nullable types — but for C and C++. Opt-in, zero runtime cost, [17-26% marginal compile-time overhead](PERFORMANCE.md) when `-Wuninitialized` is already enabled (vs 86-130% for `-Wthread-safety`).

> **[Try it in the interactive playground](https://cs01.github.io/llvm-project/)**

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
# zero warnings
```

OK, Clang already has `_Nullable` and `_Nonnull` annotations — let's use them:

```c
// file.c
int deref(int * _Nullable p) {
    return *p;  // crashes if p is NULL
}
```

```bash
$ clang -Wall -Wextra -Wnullability -c file.c
# still zero warnings
```

The annotation is right there. The dereference is unchecked. Clang doesn't care.

**That's why this fork exists.**

```
$ nullsafe-clang -fflow-sensitive-nullability -fnullability-default=nullable file.c

warning: dereference of nullable pointer [-Wflow-nullable-dereference]
    return *p;
            ^
note: add a null check before dereferencing, or annotate as '_Nonnull' if this pointer cannot be null
```

Add a null check, and the warning goes away:

```c
int deref(int * _Nullable p) {
    if (!p) return 0;
    return *p;  // OK — p is proven non-null
}
```

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
| `-fnullability-default=unspecified` | Default. Warnings only inside `#pragma assume_nonnull` regions |
| `-fnullability-default=nullable` | All unannotated pointers are nullable. Maximum checking |
| `-fnullability-default=nonnull` | All unannotated pointers are nonnull. Ergonomic mode |

## What it catches

| Pattern | Stock Clang | Static Analyzer | Nullsafe Clang |
|---------|:-----------:|:---------------:|:--------------:|
| `_Nullable` → `_Nonnull` conversion | ✅ warns | ✅ warns | ✅ warns |
| Deref of `_Nullable` pointer (`*p`, `p->x`, `p[i]`) | ❌ silent | ✅ warns | ✅ warns |
| Deref of unchecked parameter | ❌ silent | ❌ silent | ✅ warns |
| Struct member deref (`head->next->value`) | ❌ silent | ✅ warns | ✅ warns |
| Smart pointer after `reset()` / `std::move()` | ❌ silent | ❌ silent | ✅ warns |
| Works on unannotated code | ❌ | ❌ | ✅ |
| Runs in IDE (clangd) | ✅ | ❌ | ✅ |
| Fast enough for every build | ✅ | ❌ | ✅ |
| Cross-function reasoning | — | ✅ | ❌ |
| Zero compile-time cost | ✅ | — | ❌ ([17-26% marginal](PERFORMANCE.md)) |

**Safe code recognized (no false positives):**

| Pattern | Nullsafe Clang |
|---------|:--------------:|
| Null check (`if (p)`, `if (!p) return`) | ✅ |
| Assertion macro (any `[[noreturn]]` call) | ✅ |
| Loop narrowing (`while (p)`, `for (;p;)`) | ✅ |
| Ternary / short-circuit (`p ? *p : 0`, `p && *p`) | ✅ |
| Struct member check (`if (n->next)`) | ✅ |
| Smart pointer check (`if (sp)`) | ✅ |
| `make_unique` / `make_shared` / `new` result | ✅ |
| `this->member` access | ✅ |

## Gradual adoption

The analysis only activates for functions that opt in. There are two ways to opt in:

**1. `#pragma clang assume_nonnull`** — wrap a region of code:

```c
#pragma clang assume_nonnull begin

void api_function(int* _Nullable input) {
    *input = 42;  // warning: input is _Nullable
}

#pragma clang assume_nonnull end

void legacy_function(int* p) {
    *p = 42;  // no warning — outside the pragma
}
```

**2. `-fnullability-default=nullable`** — enable for the whole file:

```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable file.c
```

This lets you migrate one function, one file, or one module at a time.

**Tip:** Run `-fnullability-default=nullable` against a compilation database (`compile_commands.json`) as an analysis step — like a linter — without blocking builds. This surfaces every potential null dereference in the codebase, and you can fix them incrementally.

## Annotated standard library headers

Nullability-annotated `stdlib.h`, `stdio.h`, and `string.h` are included. These annotate `malloc` as returning `_Nullable`, `free` as accepting `_Nullable`, etc:

```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable \
      -I/path/to/clang/nullsafe-headers/include file.c
```

## Deeper dives

- **[Architecture Diagrams](docs/flow-nullability-architecture.md)** — Mermaid flow diagrams of the three-layer design, worklist algorithm, state tracking, and transfer functions
- **[Architecture Review Guide](docs/flow-nullability-review-guide.md)** — written walkthrough with concrete code examples for every concept
- **[Performance Benchmarks](PERFORMANCE.md)** — compile-time overhead measurements with statistical analysis (paired t-test, 95% confidence intervals)

## "Doesn't Clang already do this?"

| Tool | What it does | Gap |
|------|-------------|-----|
| `-Wnullability` | Warns on `_Nullable` → `_Nonnull` **conversions** | Doesn't warn on *dereferences* |
| Static Analyzer (`core.NullDereference`) | Symbolic execution, path-sensitive | Separate tool, slow, no IDE support, no `-fnullability-default` equivalent |
| ASan / UBSan | Runtime crash detection | Requires test coverage, runtime only |

Nullsafe Clang runs **inside the compiler** as a fast forward dataflow pass — same architecture as `-Wthread-safety`. It works in clangd, runs on every build, and catches bugs on unannotated code with `-fnullability-default=nullable`. Compare all three in the **[interactive playground](https://cs01.github.io/llvm-project/)**.

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
