# Nullsafe Clang

**Compile-time null safety checking for C and C++.**

A fork of Clang that adds flow-sensitive nullability analysis. It catches null pointer bugs at compile time — the same way TypeScript catches `undefined` access or Kotlin catches nullable types — but for C and C++. Opt-in, zero runtime cost, [negligible compile-time overhead](PERFORMANCE.md#real-world-benchmarks-llvmclang) — 41x faster than the Clang Static Analyzer.

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
| `_Nullable` → `_Nonnull` conversion | ✅ warns (type-based) | ✅ warns | ✅ warns (flow-aware) |
| Dereference of nullable pointer | ❌ silent | ✅ warns | ✅ warns |
| Arithmetic on nullable pointer | ❌ silent | ❌ silent | ✅ warns |
| Works on unannotated code | ❌ | ❌ | ✅ |
| Runs as part of compiler | ✅ | ❌ | ✅ |
| Runs in IDE (clangd) | ✅ | ❌ | ✅ |
| Fast enough for every build | ✅ | ❌ ([41x slower on real code](PERFORMANCE.md#csa-comparison-on-real-code)) | ✅ |
| No test coverage required | ✅ | ✅ | ✅ |
| Cross-function reasoning | — | ✅ | ✅ intra-TU (call graph + annotations) |
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
clang -fflow-sensitive-nullability -fnullability-default=nullable -Werror=flow-nullability file.c
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

## Warning groups

All warnings are under the `-Wflow-nullability` umbrella:

| Warning group | What it catches |
|---|---|
| `-Wflow-nullable-dereference` | `*p`, `p->m`, `p[i]` on nullable pointer |
| `-Wflow-nullable-arithmetic` | `p + n`, `p++`, `p += n` on nullable pointer |
| `-Wflow-nullable-return` | returning nullable from nonnull function |
| `-Wflow-nullable-assignment` | assigning nullable to nonnull variable |
| `-Wflow-nullable-argument` | passing nullable to nonnull parameter |

When `-fflow-sensitive-nullability` is enabled, the type-based `-Wnullable-to-nonnull-conversion` is automatically suppressed — the flow-sensitive checks provide strictly better coverage (they respect null checks and narrowing).

## Cross-function narrowing

While the core analysis is intraprocedural, nullsafe supports several mechanisms for cross-function reasoning:

- **`_Nonnull` parameter narrowing** — passing a pointer to a function parameter marked `_Nonnull` narrows the pointer to non-null after the call. If the function requires `_Nonnull` and your code survived the call, the pointer was non-null.
- **Member pointer narrowing** — null checks on `this->member` persist across the function body. After `if (ptr->field)`, dereferences through `field` are clean.
- **Intra-TU all-returns-nonnull inference** — the analysis runs over the entire translation unit using call-graph ordering (Tarjan's SCC algorithm). Callees are always analyzed before their callers, regardless of source order. When every return path in a function is provably non-null, callers automatically narrow the return value — no annotation needed, no source-order dependence.

```cpp
// Caller defined first — still works because the analysis uses call-graph
// order, not source order.
void use() {
    Widget* w = make_widget();  // narrowed to nonnull
    w->render();                // no warning
}

Widget* make_widget() {
    return new Widget();  // always non-null (throwing new)
}
```

Mutually recursive functions are detected as strongly connected components (SCCs) and conservatively excluded from all-returns-nonnull inference — the analysis would need fixpoint iteration within the SCC to get it right. Warnings for individual dereferences within recursive functions are still emitted normally.

- **`_Nonnull` parameter narrowing example:**

```cpp
void process(Widget* _Nonnull w);

Widget* p = get_widget();  // nullable
process(p);                // passes p to _Nonnull — narrows p
p->render();               // no warning — p is proven non-null by the call above
```

### Evidence remarks for cross-TU annotation inference

The compiler can emit `-Rnullsafe-evidence` remarks that report what the analysis observed about each function. These are opt-in diagnostic remarks, not warnings.

```bash
clang -fflow-sensitive-nullability -Rnullsafe-evidence file.cpp
```

Three kinds of evidence are emitted:

| Evidence | Remark format | What it observes |
|---|---|---|
| **Member assignment** | `member 'X' of 'Y' assigned from nonnull source` | How class members are initialized/assigned |
| **Function return** | `function 'X' of 'Y' returns nonnull` | What functions return across all paths |
| **Parameter call-site** | `parameter 'X' of 'Y' called with nonnull argument` | What callers pass to function parameters |

External tooling can aggregate these remarks across translation units to automatically infer `_Nonnull`/`_Nullable` annotations for headers. If a parameter is always called with nonnull arguments across thousands of files, it should be annotated `_Nonnull` — eliminating downstream false-positive warnings without manual annotation.

### Built-in STL nullability knowledge

The analysis has built-in knowledge that certain C++ standard library methods always return non-null pointers:

- `std::vector::data()`, `begin()`, `end()`
- `std::basic_string::c_str()`, `data()`, `begin()`, `end()`
- `std::basic_string_view::begin()`, `end()` (but NOT `data()` — intentionally nullable since `string_view` can be constructed from `nullptr`)
- `std::optional::operator->()` (undefined behavior if empty, so nonnull contract)
- `std::array::data()`, `begin()`, `end()`
- `std::span::data()`, `begin()`, `end()`

This eliminates false-positive warnings from STL usage without requiring header annotations.

## Limitations

- **Intra-TU only** — call-graph-based inference works within a single translation unit. Cross-TU contracts are expressed with `_Nonnull`/`_Nullable` annotations (which can be inferred via `-Rnullsafe-evidence` remarks and external tooling).
- **Null safety only** — doesn't catch buffer overflows, use-after-free, or other memory bugs.
- **Pointer casts** — explicit pointer-to-pointer casts (C-style, `static_cast`, `reinterpret_cast`) propagate the operand's nullability and narrowing, both at dereference and in branch conditions (`if ((T*)p) { *p; }` narrows `p`). A cast from a non-pointer source (e.g. `reinterpret_cast<T*>(some_integer)`) cannot inherit nullability and follows the default.

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

## Implementation overview

The analysis is a forward, intraprocedural dataflow pass over Clang's CFG (control flow graph) — it analyzes one function at a time, following the same architecture as the existing ThreadSafety and UninitializedValues analyses. It does not use MLIR or ClangIR — it operates on the AST-level CFG that Clang already builds for its existing warnings infrastructure. Cross-function reasoning is handled separately via call-graph ordering and annotations (see "Cross-function evidence" below).

### Lattice

The abstract state at each program point is a `NullState` containing:

- **Narrowed sets** — pointers proven non-null by control flow (null checks, nonnull init, etc.)
- **Nullable sets** — pointers known to hold nullable values
- **Auxiliary maps** — bool guards, aliases, address-of targets (described below)

The core logic is as follows: If a pointer is in a nullable set (or has nullable type) and is NOT in a narrowed set, dereferencing it is a warning.

#### Narrowed sets

There are two narrowed sets, one for plain variables and one for member access chains:

`NarrowedVars` is a `DenseSet<const VarDecl*>` for local variables and parameters. When you write `if (p)`, the variable `p` is added to this set on the true branch.

`NarrowedMembers` is a `DenseSet<MemberAccessPath>` for field accesses. A `MemberAccessPath` is a small struct: a `const VarDecl*` root plus a `SmallVector<const FieldDecl*>` chain of fields, compared element-wise by pointer identity. So `s->x` is `{Root=s, Fields=[x]}`, `o.inner.x` is `{Root=o, Fields=[inner, x]}`, and `this->field` uses a null root as a sentinel (safe because the analysis is intraprocedural — `this` is always the same object within a single function, and `FieldDecl` pointers are unique per class).

#### Nullable sets

`NullableVars` is a `DenseSet<const VarDecl*>` tracking variables known to hold nullable values. `NullableThisMembers` is a `DenseSet<const FieldDecl*>` that tracks `this->` smart pointer members that become nullable at runtime after `reset()` or `std::move()`.

#### Auxiliary tracking

- **Bool guards**: `bool ok = (p != nullptr)` lets a later `if (ok)` narrow `p`.
- **Aliases**: `q = p` means narrowing either one narrows both.
- **Address-of targets**: `pp = &p` means a store through `*pp` invalidates `p`'s narrowing.

#### Merging

At control flow join points, narrowed and nullable sets merge differently. Narrowed uses intersection — a pointer is only narrowed after a merge if ALL incoming paths agree. Nullable uses union — if a pointer was nullable on ANY incoming path, it stays nullable. This is conservative in both directions: won't lose track of a potential null source, and won't claim a pointer is safe unless every path proved it.

### Per-edge state tracking

Rather than storing one state per block, the analysis stores state per CFG edge (`EdgeStates[{PredBlockID, SuccBlockID}]`). This is what makes branch-sensitive narrowing work: after `if (p)`, the true and false edges carry different narrowing information. Entry state for each block is computed by merging all predecessor edge states using the join rules above.

### Transfer functions

The analysis walks each CFG block statement-by-statement:

**Dereferences** (`*p`, `p->m`, `p[i]`, `p + n`): if `p` is nullable and not narrowed, emit a warning.
**Null checks** (`if (p)`, `if (p != nullptr)`): the true-edge state adds `p` to the narrowed set; the false edge does not (and vice versa for `if (!p)`).
**Assignments** (`p = expr`): if the RHS is nonnull, narrow; if nullable, remove from the narrowed set and add to the nullable set.
**Declarations** (`int *p = nonnull_expr`): narrow at initialization.
**Assertions / early returns**: `if (!p) return;` narrows `p` in the post-dominating code, since execution only continues when `p` is non-null.

A `decomposeMemberAccess()` helper walks any `MemberExpr` chain to its root (`DeclRefExpr` or `CXXThisExpr`), collecting `FieldDecl`s along the way. This is used uniformly for both single-level (`s.x`) and nested (`o.inner.x`) member accesses — the same code path handles all depths.

Compound conditions (`&&`, `||`) are handled naturally by the CFG, which decomposes them into separate blocks with edges for short-circuit evaluation. One subtlety: the CFG terminator for each decomposed block is still the full compound expression (e.g., `p && q`), not the individual leaf. A helper `getTerminalCondition()` recursively follows the RHS of `&&`/`||` chains to find the leaf sub-expression actually being evaluated in that block — this is what lets per-edge narrowing apply to the correct variable at each branch point.

### Iteration

The analysis processes CFG blocks in reverse-post-order using a worklist. It repeats until the state stabilizes (fixpoint iteration) — if processing a block changes the outgoing state, its successors are re-enqueued. In practice, most functions converge in a single pass. Loops may require a second iteration, but since the lattice is finite (narrowed sets can only shrink at merge points, nullable sets can only grow) and monotone, convergence is guaranteed and fast.

### Cross-function evidence within a TU

Functions are analyzed in reverse call-graph order within each translation unit, using Tarjan's SCC algorithm. This means callees are always analyzed before their callers, regardless of source order. If a function is proven to return nonnull on all paths (without requiring annotation), that evidence is recorded and callers automatically narrow the return value — no annotation needed.

### Complexity

The analysis is linear in practice, O(n · h) worst-case — where n is the number of CFG blocks and h is the lattice height (bounded by the number of tracked pointers). There is no path enumeration, no constraint solving, and no exponential blowup. This is a deliberate tradeoff: a SAT-based approach (like the Clang Static Analyzer) can reason about deeper inter-variable relationships, but at a cost that makes it impractical to run on every compilation. This analysis is lightweight enough to run as part of a normal build with no measurable compile-time impact, catching the large majority of real-world null dereferences — unchecked nullable pointer used directly — with zero false positives from post-null-check code.

### Code layout

| File | Role |
|---|---|
| lib/Analysis/FlowNullability.cpp | The analysis: CFG walk, transfer functions, edge state, fixpoint |
| include/clang/Analysis/Analyses/FlowNullability.h | Handler interface (FlowNullabilityHandler) and entry point |
| lib/Sema/AnalysisBasedWarnings.cpp | Glue: builds CFG, runs analysis, converts callbacks to S.Diag() calls |
| lib/Sema/SemaDecl.cpp | Gradual adoption: decides per-function whether to enable the analysis |

## License

Same as LLVM — Apache 2.0 with LLVM Exceptions.
