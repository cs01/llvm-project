# Nullsafe C

**Compile-time null pointer dereference checking for C and C++.**

[![Test Null-Safety](https://github.com/cs01/llvm-project/actions/workflows/test-null-safety.yml/badge.svg)](https://github.com/cs01/llvm-project/actions/workflows/test-null-safety.yml) &nbsp; [![Try Online](https://img.shields.io/badge/try-online-brightgreen)](https://cs01.github.io/llvm-project/)

A fork of Clang that adds flow-sensitive nullability analysis to the compiler. It catches null pointer dereferences at compile time — the same way TypeScript catches `undefined` access or Kotlin catches nullable types — but for C and C++.

> **[Try it in the interactive playground](https://cs01.github.io/llvm-project/)**

## The problem

Stock Clang compiles this with zero warnings, even with `-Wall -Wextra`:

```c
struct Config { int timeout; };

void apply(struct Config* _Nullable cfg) {
    cfg->timeout = 30;  // crash if cfg is NULL
}
```

The pointer is annotated `_Nullable`. The dereference is unchecked. No warning.

**With Nullsafe C:**

```
warning: dereferencing nullable pointer [-Wflow-nullable-dereference]
    cfg->timeout = 30;
         ^
```

Add a null check, and the warning goes away:

```c
void apply(struct Config* _Nullable cfg) {
    if (!cfg) return;
    cfg->timeout = 30;  // OK — cfg is proven non-null
}
```

## "Doesn't Clang already do this?"

Partially. Clang has several null-related tools, but none of them do what this fork does.

### `-Wnullability` (stock Clang)

Checks **type conversions only** — warns when you pass a `_Nullable` pointer to a `_Nonnull` parameter. This is useful and Nullsafe C preserves it. But `-Wnullability` doesn't warn on *dereferences*. You can write `*p` where `p` is `_Nullable` and get zero warnings.

### Clang Static Analyzer (`--analyze` / clang-tidy)

A separate tool that uses **symbolic execution** to explore paths. Its `nullability.NullableDereferenced` checker is actually quite good — **on annotated code, it catches the same bugs Nullsafe C does**, including flow-sensitive patterns like "checked the wrong pointer" and "forgot to return after null check." It understands flow narrowing and path conditions.

The catch is bootstrapping. CSA's null checker only fires on pointers annotated `_Nullable`. On unannotated code — which is virtually all existing C/C++ — it finds almost nothing. There's no equivalent of `-fnullability-default=nullable` to treat unannotated pointers as nullable. You'd have to annotate your codebase first to benefit from the analysis, but you need the analysis to know where to annotate.

Other limitations:

- **Separate tool** — not part of the normal compile. You have to run `--analyze` or use clang-tidy, which wraps the same engine. Must be version-compatible with your project's compiler.
- **Slow** — symbolic execution is too expensive for every build.
- **Not in your IDE** — clangd doesn't run the static analyzer.

### ASan / TSan / UBSan

**Runtime** sanitizers. They find bugs by executing the code and observing crashes. They require test coverage to be effective and have runtime overhead. Nullsafe C catches bugs before the code ever runs.

### What Nullsafe C adds

Where CSA uses symbolic execution (whole-program path exploration), Nullsafe C uses **forward dataflow analysis** — a single linear pass over the CFG, same algorithm as `-Wuninitialized`. This has three practical consequences:

- **Fast enough to run on every build.** Dataflow analysis merges states at join points rather than exploring each path individually, so it doesn't explode with branching the way symbolic execution does. It runs inside the compiler as part of the normal compile, not as a separate tool.
- **In your IDE.** Because it's part of the compiler, clangd picks it up. You get squiggly lines as you type.
- **Works on unannotated code.** With `-fnullability-default=nullable`, every unannotated pointer is treated as nullable. You don't need to annotate first — you can run the analysis against an existing codebase and fix incrementally.

## How it works

The analysis runs at the same compiler layer as `-Wuninitialized` and `-Wthread-safety`: a forward dataflow pass over the control flow graph (CFG) during semantic analysis (Sema). It tracks which pointers have been null-checked and which haven't, updating that knowledge at branches, loops, and assignments.

It understands:

```c
struct Node {
    int value;
    struct Node* _Nullable next;
};

void example(struct Node* _Nullable head) {
    if (!head) return;       // early return narrows head to nonnull
    head->value = 1;         // OK

    head->next->value = 2;   // warning — next is _Nullable

    if (head->next) {
        head->next->value = 2;  // OK — checked
    }
}
```

This is **intraprocedural** — it analyzes one function at a time. This is what makes it fast, and it's also what makes it *useful*: you can look at a single function and know whether it's safe, without having to fit the whole program in your head. Function calls don't invalidate narrowing — if you proved `p != NULL` before a call, it's still non-null after. This is sound for pass-by-value (the common case), though a function receiving `&p` could theoretically null it.

## What it catches

**Warnings (bugs found):**

| Pattern | Stock Clang | Nullsafe C |
|---------|:-----------:|:----------:|
| Passing `_Nullable` to `_Nonnull` param | **warns** | **warns** |
| Deref of `_Nullable` pointer (`*p`, `p->x`, `p[i]`) | silent | **warns** |
| Deref of unchecked parameter | silent | **warns** |
| Smart pointer use after `reset()` / `std::move()` | silent | **warns** |

**No false positives (safe code recognized):**

| Pattern | Nullsafe C |
|---------|:----------:|
| Deref after null check (`if (p)`) | ✅ no warning |
| Deref after early return (`if (!p) return`) | ✅ no warning |
| Deref after assertion (`ASSERT(p)`) | ✅ no warning |
| While-loop narrowing (`while (p)`) | ✅ no warning |
| Ternary narrowing (`p ? *p : 0`) | ✅ no warning |
| `&&` short-circuit (`p && *p`) | ✅ no warning |
| Struct member check (`if (n->next)`) | ✅ no warning |
| Smart pointer check (`if (sp)`) | ✅ no warning |
| `make_unique` / `make_shared` result | ✅ no warning |
| `new` expression result | ✅ no warning |
| `this->member` access | ✅ no warning |

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

This lets you migrate one function, one file, or one module at a time. Unannotated code in `unspecified` mode (the default) produces zero warnings.

**Tip:** A powerful workflow is to run `-fnullability-default=nullable` against a compilation database (`compile_commands.json`) as an analysis step — like a linter — without blocking builds. This surfaces every potential null dereference in the codebase, and you can fix them incrementally without changing your build flags.

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

## Installation

```bash
curl -fsSL https://raw.githubusercontent.com/cs01/llvm-project/null-safe-c-dev/install.sh | bash
```

Or download from [releases](https://github.com/cs01/llvm-project/releases). Includes `clang` and `clangd`.

### Build from source

```bash
git clone git@github.com:cs01/llvm-project.git
cd llvm-project
git checkout null-safe-c-dev
cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_USE_LINKER=lld
ninja -C build clang clangd
```

## Advanced features

### Struct member tracking

The analysis tracks nullability through struct member accesses — essential for linked lists and trees:

```c
typedef struct Node {
    int value;
    struct Node* _Nullable next;
} Node;

void traverse(Node* _Nonnull head) {
    for (Node* _Nullable p = head; p; p = p->next) {
        p->value = 0;      // OK — p checked by loop condition
    }
}

void buggy(Node* _Nonnull head) {
    head->next->value = 1;  // warning: head->next is _Nullable
}
```

### Smart pointer awareness

Tracks `std::unique_ptr` and `std::shared_ptr` through `reset()`, `std::move()`, `make_unique`, and `make_shared`:

```cpp
void example(std::unique_ptr<Node> sp) {
    if (sp) sp->value = 1;   // OK — checked

    sp.reset();
    sp->value = 1;            // warning — reset made it nullable

    auto other = std::move(sp);
    sp->value = 1;            // warning — moved from
}
```

### Assertion macros

Any assertion pattern that calls a `[[noreturn]]` function narrows:

```c
[[noreturn]] void die(const char* msg);
#define ASSERT(x) do { if (!(x)) die("fail"); } while(0)

void example(int* _Nullable p) {
    ASSERT(p);
    *p = 42;  // OK — ASSERT proved p is non-null
}
```

### Annotated standard library headers

Nullability-annotated versions of `stdlib.h`, `stdio.h`, and `string.h` are included in `clang/nullsafe-headers/`. Use with:

```bash
clang -fflow-sensitive-nullability -fnullability-default=nullable \
      -I/path/to/clang/nullsafe-headers/include file.c
```

These annotate functions like `malloc` as returning `_Nullable` and `free` as accepting `_Nullable`.

## Architecture

Three layers, following the same pattern as `-Wthread-safety`:

| Layer | File | Role |
|-------|------|------|
| Analysis | `lib/Analysis/FlowNullability.cpp` | Forward dataflow on the CFG. Tracks narrowing sets, reports dereferences via callback |
| Glue | `lib/Sema/AnalysisBasedWarnings.cpp` | Builds CFG, runs analysis, converts callbacks to diagnostics |
| Gating | `lib/Sema/SemaDecl.cpp` | Decides per-function whether to enable analysis |

The analysis uses per-edge state tracking (`EdgeStates[{pred, succ}]`) so branch-refined narrowing propagates correctly. Entry state for each block is the intersection of all predecessor edge states — a pointer is only considered narrowed if ALL paths agree.

## Limitations

- **Intraprocedural** — does not look inside called functions. This is by design: it's what makes the analysis fast, and it means you can reason about one function at a time. Cross-function contracts are expressed with `_Nonnull` and `_Nullable` annotations on function signatures.
- **No alias tracking** — if two pointers alias the same memory, modifying one won't invalidate the other's narrowing.
- **No inferred return nullability** — the analysis doesn't infer that a function "always returns non-null" from its body. Annotate return types with `_Nonnull` or `_Nullable` to express that.
- **Null dereferences only** — this doesn't catch buffer overflows, use-after-free, or other memory bugs. For those, consider sanitizers (ASan), `-fbounds-safety`, or a memory-safe language.

## License

Same as LLVM — Apache 2.0 with LLVM Exceptions.
