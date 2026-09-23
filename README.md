# Clang Nullability Safety

**Compile-time null pointer checking for C and C++.**

This is a fork of Clang that adds **Nullability Safety**, a flow-sensitive analysis that reports null pointer bugs while you compile. It tracks null checks through each function, the way TypeScript narrows `undefined` or Kotlin narrows nullable types, and warns when a pointer that may be null is used without a check. It is opt-in, has no runtime cost, and on real LLVM/Clang sources it accounts for [0.2-8% of compile time](PERFORMANCE.md#direct-measurement-via--ftime-trace) (median about 2%).

The name follows Clang's existing `-Wthread-safety` analysis: it is named for the property it checks. Like thread safety analysis, it is not a soundness proof; see [Limitations](#limitations).

> **[Try it in the online playground](https://cs01.github.io/llvm-project/)**

## The problem

Stock Clang does not warn about this dereference, whatever flags you pass:

```c
// file.c
int deref(int *p) {
    return *p;  // crashes if p is NULL
}
```

```bash
$ clang -Wall -Wextra -Wnullability -Wnull-dereference -c file.c
```

Clang already has `_Nullable` and `_Nonnull` annotations, so annotate the parameter:

```c
// file.c
int deref(int * _Nullable p) {
    return *p;  // crashes if p is NULL
}
```

```bash
$ clang -Wall -Wextra -Wnullability -c file.c
```

Still no warning. Stock Clang uses these annotations to check conversions and null constants, not dereferences.

With this fork and `-fnullability-safety`, the same code warns during normal compilation:

```
$ clang -fnullability-safety -c file.c
file.c:2:12: warning: dereference of nullable pointer 'int * _Nullable' [-Wnullability-safety-dereference]
    2 |     return *p;  // crashes if p is NULL
      |            ^
file.c:2:12: note: add a null check before dereferencing, or annotate as '_Nonnull' if this pointer cannot be null
1 warning generated.
```

Add a null check and the warning goes away, because the analysis knows `p` is non-null on that path:

```c
int deref(int * _Nullable p) {
    if (!p) return 0;
    return *p;  // OK: p is non-null here
}
```

The design is discussed in the [RFC on Discourse](https://discourse.llvm.org/t/rfc-nullability-safety/89042).

## Installation

Try it without installing in the [online playground](https://cs01.github.io/llvm-project/), or build from source:

```bash
git clone git@github.com:cs01/llvm-project.git
cd llvm-project
git checkout nullability-safety
cmake -S llvm -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_USE_LINKER=lld
ninja -C build clang clangd
```

In the rest of this document, `clang` means the fork's `clang` (`build/bin/clang`).

## Usage

```bash
# Gradual: check only annotated functions and assume_nonnull regions
clang -fnullability-safety file.c

# Strict: treat every unannotated pointer as nullable
clang -fnullability-safety -fnullability-default=nullable file.c

# Ergonomic: treat every unannotated pointer as nonnull; annotate only what can be null
clang -fnullability-safety -fnullability-default=nonnull file.c

# Make the warnings errors
clang -fnullability-safety -Werror=nullability-safety file.c
```

### Flags

| Flag | Description |
|------|-------------|
| `-fnullability-safety` | Enable the analysis. Nothing else here has an effect without it |
| `-fnullability-default=unspecified` | The default. Only functions with `_Nullable`/`_Nonnull` annotations and code inside `#pragma clang assume_nonnull` are checked |
| `-fnullability-default=nullable` | Every unannotated pointer is nullable and every function is checked. Most warnings |
| `-fnullability-default=nonnull` | Every unannotated pointer is nonnull and every function is checked. Annotate only what can be null, as in Kotlin and Swift |
| `-fno-nullability-libc-nullable-returns` | Stop treating `malloc`, `fopen`, `getenv`, `strchr` and similar C library functions as returning nullable (see [Standard library knowledge](#standard-library-knowledge)) |

The `-fnullability-*` flags must match across a translation unit and any precompiled headers or modules it uses.

### Adopting gradually

In the default mode, a function is checked if it, or any of its declarations, has a `_Nullable` or `_Nonnull` annotation on a parameter or the return type. Everything else is left alone:

```c
void legacy(int *p) {
    *p = 42;  // not checked
}

void checked(int * _Nullable p) {
    *p = 42;  // warning: p is _Nullable
}
```

Clang's `assume_nonnull` pragma opts in a whole region. Inside it, unannotated pointers are `_Nonnull`, so you mark only the nullable ones:

```c
#pragma clang assume_nonnull begin
void api_function(int *out, int * _Nullable input) {
    *out = 1;      // OK: out is _Nonnull
    *input = 42;   // warning: input is _Nullable
}
#pragma clang assume_nonnull end
```

You can migrate one function, one file, or one module at a time.

### In the build or as a separate pass

- **In the build:** add `-fnullability-safety` to your compiler flags. Warnings appear with every other diagnostic, including in your editor through clangd.
- **As a lint pass:** run the fork with `-fsyntax-only -fnullability-default=nullable` over each entry of a compilation database. No object files are produced and your build is untouched, so you can work through the findings at your own pace.

## Warnings

All warnings are in the `-Wnullability-safety` group:

| Warning group | What it catches |
|---|---|
| `-Wnullability-safety-dereference` | `*p`, `p->m`, `p[i]` on a pointer that may be null |
| `-Wnullability-safety-arithmetic` | `p + n`, `p++`, `p += n` on a pointer that may be null |
| `-Wnullability-safety-return` | returning a pointer that may be null from a `_Nonnull` function |
| `-Wnullability-safety-assignment` | assigning a pointer that may be null to a `_Nonnull` variable |
| `-Wnullability-safety-argument` | passing a pointer that may be null to a `_Nonnull` parameter |

With `-fnullability-safety`, Clang's type-based `-Wnullable-to-nonnull-conversion` is turned off. The argument, assignment, and return warnings above cover the same cases, and they know when a null check has made a `_Nullable` pointer safe.

## How it compares

| | Stock Clang (`-Wnullability`) | Clang Static Analyzer | Nullability Safety |
|--|:-----------------------------:|:---------------------:|:--------------:|
| Technique | Type checking | Path-sensitive symbolic execution | Dataflow over the CFG |
| `_Nullable` to `_Nonnull` conversion | ✅ (ignores null checks) | ✅ | ✅ (respects null checks) |
| Dereference of `_Nullable` pointer | ❌ | ✅ | ✅ |
| Arithmetic on `_Nullable` pointer | ❌ | ❌ | ✅ |
| Can treat unannotated pointers as nullable | ❌ | ❌ | ✅ (`-fnullability-default=nullable`) |
| Runs during normal compilation and in clangd | ✅ | ❌ | ✅ |
| Cross-function reasoning | none | inlines callees it can see | within the translation unit (call graph and annotations) |
| Cost | none | [1.95x baseline on 24 LLVM files, 41x slower than Nullability Safety on `ExprConstant.cpp`](PERFORMANCE.md#csa-comparison-on-real-code) | [0.2-8% of compile time](PERFORMANCE.md#direct-measurement-via--ftime-trace) |

The analysis works like `-Wthread-safety` and `-Wuninitialized`: one forward pass over each function's control flow graph, part of ordinary compilation. The Static Analyzer explores paths separately, so it can find bugs that depend on how two variables relate to each other, but it is too slow to run on every build. The [playground](https://cs01.github.io/llvm-project/) runs all three side by side, and [nullability-safety-vs-csa.md](playground/nullability-safety-vs-csa.md) explains the difference in detail.

ASan and UBSan complement this. They are runtime sanitizers: they need a test that actually reaches the bug, add roughly 2x overhead, and report the crash when it happens instead of at compile time.

## Reasoning across functions

The analysis looks at one function at a time, but it uses the following to reason about calls:

- **Callees that always return non-null.** Within a translation unit, functions are analyzed callees-first, using the call graph (Tarjan's strongly connected components algorithm), so source order does not matter. If every return in a function is provably non-null, callers treat its result as non-null without any annotation:

  ```cpp
  // use() comes first, but make_widget() is analyzed before it.
  void use() {
      Widget* w = make_widget();  // known non-null
      w->render();                // no warning
  }

  Widget* make_widget() {
      return new Widget();  // throwing new never returns null
  }
  ```

  Mutually recursive functions form a cycle in the call graph. They are still checked, but none of them is inferred to always return non-null.

- **`_Nonnull` parameters.** After a pointer is passed to a `_Nonnull` parameter, the analysis treats it as non-null. The call is where the warning is reported, once, rather than at every later use:

  ```cpp
  void process(Widget* _Nonnull w);

  void f(Widget* _Nullable p) {
      process(p);   // warning: passing nullable pointer to nonnull parameter
      p->render();  // no second warning
  }
  ```

- **Members.** A null check on `this->field` or `s->field` holds for the rest of the function, until the member is assigned.

Across translation units, contracts come from `_Nonnull`/`_Nullable` annotations in headers.

### Inferring annotations

The analysis can infer `_Nonnull` and `_Nullable` for parameters, fields and function returns across a whole program and write them into the source. This uses Clang's Scalable Static Analysis Framework (SSAF):

```bash
# 1. For each translation unit: record evidence and pointer flow.
clang -fsyntax-only -fnullability-default=nonnull a.c \
  --ssaf-extract-summaries=PointerFlow,NullabilitySafety \
  --ssaf-compilation-unit-id=a --ssaf-tu-summary-file=a.json
# 2. Link the summaries and infer.
clang-ssaf-linker a.json b.json -o lu.json
clang-ssaf-analyzer lu.json -o wpa.json -a NullabilityInferenceAnalysisResult
# 3. For each translation unit: write edits and a SARIF report.
clang -fsyntax-only -fnullability-default=nonnull a.c \
  --ssaf-source-transformation=nullability-annotations \
  --ssaf-global-scope-analysis-result=wpa.json \
  --ssaf-src-edit-file=edits/a.yaml --ssaf-transformation-report-file=a.sarif \
  --ssaf-compilation-unit-id=a --ssaf-link-unit-id=lu
# 4. Merge the edits (a shared header is edited once) and apply them.
clang-ssaf-src-edit-merge edits/*.yaml -o merged/merged.yaml
clang-apply-replacements merged
```

`--ssaf-link-unit-id` must be the stem of the linker's output file (`lu` for `lu.json`).

Only `_Nonnull` is written: on each parameter, field and function return where every observed value is non-null and no value that may be null can reach it through pointer flow. Evidence distinguishes a value that is null on every path to the use (for example `f(NULL)`) from one that is null on some path only (for example a local set on the success path, with the error path returning first). The second kind never supports `_Nullable`, but it still rules out `_Nonnull`.

The SARIF report lists what is not written:

- parameters and returns inferred `_Nullable`, as suggestions to review
- every store of null into a field (a field is typically null only before setup or after teardown, so `_Nullable` on it would warn at every use)
- pointers a nullable value may reach through pointer flow, and declarations the tool cannot rewrite (spelled through a macro, `auto`, inner pointer levels)

On sqlite, applying every edit leaves the `-fnullability-default=nonnull` warnings unchanged.

Caveats:

- Once a header has some nullability annotations, compilers that don't use `-fnullability-default` warn about the remaining unannotated pointers in it (`-Wnullability-completeness`).
- Evidence observed inside Objective-C method bodies is not recorded: SSAF has no entities for Objective-C methods.

## Standard library knowledge

**C library.** The analysis treats the results of C library functions that return null on failure as `_Nullable`, even with your system's headers: `malloc`, `calloc`, `realloc`, `aligned_alloc`, `fopen`, `freopen`, `tmpfile`, `getenv`, `strtok`, `strstr`, `strchr`, `strrchr`, `strpbrk`, `memchr`, `bsearch`, `tmpnam`, and `setlocale`. `-fno-nullability-libc-nullable-returns` turns this off.

Parameter contracts (for example, that `strcpy` needs non-null arguments) come from `__attribute__((nonnull))` in your libc's headers, which glibc declares and the analysis honors.

**C++ library.** These methods are known to return non-null pointers, so using their results doesn't warn. This list only removes warnings, so it is always on and has no flag:

- `std::vector`: `data()`, `begin()`, `end()`
- `std::basic_string`: `c_str()`, `data()`, `begin()`, `end()`
- `std::basic_string_view`: `begin()`, `end()`. `data()` is excluded because a `string_view` can hold `nullptr`
- `std::optional::operator->()`: calling it on an empty optional is undefined behavior, so a caller already assumes a value
- `std::array<T, N>` with `N > 0`: `data()`, `begin()`, `end()`
- `std::span`: `data()`, `begin()`, `end()`. An empty span can return null here; the analysis stays silent anyway, because warning on every span would be mostly noise

`std::unique_ptr`, `std::shared_ptr`, and `std::weak_ptr` are tracked too: `make_unique`/`make_shared` produce non-null pointers, `reset()` and moving from a pointer make it nullable, and `if (sp)` counts as a null check.

## Limitations

The analysis prefers missing a bug over reporting a false one. Known gaps:

- **One translation unit at a time.** Inferred facts don't cross translation units; only annotations do.
- **Calls don't reset null checks.** After `if (p)`, `p` stays non-null even if a call in between could have changed it. `-Wthread-safety` makes the same trade-off.
- **Limited aliasing.** Only direct aliases (`q = p`) and direct address-taking (`T **pp = &p; *pp = ...`) are tracked. Longer alias chains are not.
- **Lambdas.** Unannotated lambda parameters are treated as `_Nonnull`, whatever `-fnullability-default` says.
- **Casts.** Pointer-to-pointer casts (C-style, `static_cast`, `reinterpret_cast`) keep the operand's nullability, and a check on a cast (`if ((T*)p)`) counts as a check on `p`. A cast from an integer gets the default nullability.
- **Only null pointers.** Buffer overflows, use-after-free, and other memory bugs are out of scope.

## Editor integration

The build includes `clangd`, so warnings show up in your editor as you type.

**VS Code:** install the clangd extension, then set:
```json
{ "clangd.path": "/path/to/llvm-project/build/bin/clangd" }
```

**Neovim**, with lspconfig:
```lua
require('lspconfig').clangd.setup({
  cmd = { vim.fn.expand('/path/to/llvm-project/build/bin/clangd') }
})
```

## How it works

This section summarizes the implementation. For more detail:

- [Architecture diagrams](docs/nullability-safety-architecture.md): Mermaid diagrams of the three layers, the worklist algorithm, state tracking, and transfer functions
- [Performance](PERFORMANCE.md): LLVM/Clang measurements, synthetic stress tests, and a Static Analyzer comparison

The analysis is a forward dataflow pass over Clang's CFG (control flow graph), one function at a time, built like the existing thread safety and uninitialized-variable analyses. It uses the CFG Clang already builds for those warnings; it does not use MLIR or ClangIR.

### State

At each program point the analysis keeps a `NullState`:

- **Narrowed sets:** pointers proven non-null by the control flow (a null check, a non-null initializer, and so on)
- **Nullable sets:** pointers known to hold a value that may be null
- **Helper maps:** bool guards, aliases, and address-of targets

A pointer that is nullable (by type or because of a nullable set) and not narrowed produces a warning when it is dereferenced.

There are two narrowed sets. `NarrowedVars` is a `DenseSet<const VarDecl*>` of local variables and parameters; `if (p)` adds `p` on the true branch. `NarrowedMembers` is a `DenseSet<MemberAccessPath>`. A `MemberAccessPath` is a root `const VarDecl*` plus a `SmallVector<const FieldDecl*>` of fields, so `s->x` is `{Root=s, Fields=[x]}` and `o.inner.x` is `{Root=o, Fields=[inner, x]}`. `this->field` uses a null root; that is safe because `this` doesn't change within one function.

`NullableVars` is a `DenseSet<const VarDecl*>` of variables holding a value that may be null. `NullableThisMembers` is a `DenseSet<const FieldDecl*>` of `this->` smart pointer members that become null after `reset()` or `std::move()`.

The helper maps cover three idioms:

- **Bool guards:** `bool ok = (p != nullptr); if (ok) ...` narrows `p`.
- **Aliases:** after `q = p`, narrowing either one narrows both.
- **Address-of targets:** after `pp = &p`, a store through `*pp` drops what was known about `p`.

### Merging paths

Where control flow paths meet, the sets merge in opposite directions. Narrowed sets are intersected: a pointer stays narrowed only if every incoming path narrowed it. Nullable sets are unioned: a pointer that may be null on any incoming path may be null after the merge. Both choices are conservative.

State is stored per CFG edge (`EdgeStates[{PredBlockID, SuccBlockID}]`), not per block. That is what lets the true and false edges of `if (p)` carry different facts. A block's entry state is the merge of its incoming edges.

### Transfer functions

Each CFG block is processed one statement at a time:

- **Dereferences** (`*p`, `p->m`, `p[i]`, `p + n`): warn if `p` is nullable and not narrowed.
- **Null checks** (`if (p)`, `if (p != nullptr)`): the true edge narrows `p`, the false edge doesn't, and the reverse for `if (!p)`.
- **Assignments** (`p = expr`): a non-null right side narrows `p`; a nullable one un-narrows it and marks it nullable.
- **Declarations** (`int *p = nonnull_expr`): narrow at initialization.
- **Early exits:** after `if (!p) return;`, `p` is narrowed for the rest of the function.

`decomposeMemberAccess()` walks a `MemberExpr` chain down to its root (`DeclRefExpr` or `CXXThisExpr`), collecting each `FieldDecl`, so `s.x` and `o.inner.x` take the same code path.

The CFG splits `&&` and `||` into separate blocks for short-circuit evaluation, but each block's terminator is still the whole expression (for example `p && q`). `getTerminalCondition()` follows the right-hand side of `&&`/`||` chains to find the operand that block actually tests, so narrowing applies to the right variable.

### Iteration and reporting

Blocks are processed in reverse post-order from a worklist. When a block's outgoing state changes, its successors go back on the worklist, until nothing changes. Most functions settle in one pass and loops usually take two. The narrowed sets can only shrink and the nullable sets can only grow, so this always terminates. Warnings and evidence are reported in one final pass over the settled states, so a loop body visited before its back edge is known doesn't produce a spurious or contradictory result.

### Complexity

The worst case is O(n · h), where n is the number of CFG blocks and h is the lattice height (bounded by the number of tracked pointers); in practice it is linear. There is no path enumeration and no constraint solving. The trade-off: the Static Analyzer can reason about relationships between variables that this analysis cannot, but it is too slow for every build.

### Code layout

| File | Role |
|---|---|
| `clang/lib/Analysis/NullabilitySafety.cpp` | The analysis: CFG walk, transfer functions, edge states, fixpoint |
| `clang/include/clang/Analysis/Analyses/NullabilitySafety.h` | Handler interface (`NullabilitySafetyHandler`) and entry point |
| `clang/lib/Sema/AnalysisBasedWarnings.cpp` | Builds CFGs, orders functions by call graph, runs the analysis, turns callbacks into diagnostics |
| `clang/lib/Sema/SemaDecl.cpp` | Decides which functions are checked |

## License

Same as LLVM: Apache 2.0 with LLVM Exceptions.
