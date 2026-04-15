# Nullsafe Upstream Cheat Sheet

This note is for explaining the current PR, with special focus on the `nullsafe-upstream` branch that is intended for LLVM upstreaming.

## Short Version

`nullsafe-upstream` adds a new Clang warning pass for flow-sensitive nullability checking in C and C++.

It lets Clang use local control flow to prove when a nullable pointer has been checked, and warn when code still dereferences, returns, assigns, or passes a pointer that may be null.

The key value proposition is:

- upstreamable Clang integration, not a separate tool
- fast enough to run during normal compilation
- opt-in and gradual-adoption friendly
- materially stronger than stock Clang's existing nullability warnings
- narrower in scope than Crubit's full nullability stack

## What Is In The PR vs. What Is In `nullsafe-upstream`

The overall `nullsafe-clang-dev` branch contains three kinds of work:

- core compiler analysis work
- tests and correctness fixes around that analysis
- productization work: docs, benchmarks, playground, release/install/CI scripts, nullsafe headers, branch maintenance

The part that looks upstreamable is the `nullsafe-upstream` branch:

- 1 squashed commit on top of `llvm/main`
- 30 changed files
- about 7k inserted lines
- almost entirely in Clang analysis, sema, diagnostics, options, and tests

The broader dev branch goes much further and is useful for the fork, but it is not the clean upstream story.

## What `nullsafe-upstream` Actually Adds

### User-facing surface area

New flags:

- `-fflow-sensitive-nullability`
- `-fnullability-default=unspecified|nullable|nonnull`

New warning umbrella:

- `-Wflow-nullability`

Subgroups:

- `-Wflow-nullable-dereference`
- `-Wflow-nullable-arithmetic`
- `-Wflow-nullable-return`
- `-Wflow-nullable-assignment`
- `-Wflow-nullable-argument`

### Core implementation

The branch adds a new analysis entry point in Clang:

- [`clang/lib/Analysis/FlowNullability.cpp`](/data/users/cssmith/git/llvm-nullsafe/clang/lib/Analysis/FlowNullability.cpp)
- [`clang/include/clang/Analysis/Analyses/FlowNullability.h`](/data/users/cssmith/git/llvm-nullsafe/clang/include/clang/Analysis/Analyses/FlowNullability.h)

It is wired into Clang's normal analysis-based warning pipeline in:

- [`clang/lib/Sema/AnalysisBasedWarnings.cpp`](/data/users/cssmith/git/llvm-nullsafe/clang/lib/Sema/AnalysisBasedWarnings.cpp)

This is important: it behaves like a compiler warning pass, not like an out-of-band static analyzer run or clang-tidy tool.

### Adoption model

The analysis only runs for functions that opt in via one of these paths:

- `-fnullability-default` is set
- `#pragma clang assume_nonnull` is active
- the function signature already contains explicit nullability annotations

That gradual-adoption behavior is implemented in:

- [`clang/lib/Sema/AnalysisBasedWarnings.cpp#L3168`](/data/users/cssmith/git/llvm-nullsafe/clang/lib/Sema/AnalysisBasedWarnings.cpp#L3168)

## What It Can Do

### 1. Catch nullable dereferences with path sensitivity

It warns on:

- `*p`
- `p->field`
- `p[i]`

when `p` may be null, but suppresses the warning after a successful null check.

Example:

```c
void f(int * _Nullable p) {
  *p;        // warning
  if (!p) return;
  *p;        // OK
}
```

### 2. Track local control flow instead of just declared type

It understands:

- `if (p)`
- `if (p != nullptr)`
- `if (!p) return`
- ternaries
- loops
- assignment inside conditions
- simple boolean guard variables derived from null checks
- common abort/assert patterns through CFG structure

This is the main difference from stock Clang nullability warnings.

### 3. Check more than dereferences

It can also warn on:

- pointer arithmetic on nullable pointers
- returning nullable from a `_Nonnull` return
- assigning nullable into a `_Nonnull` variable
- passing nullable to a `_Nonnull` parameter

Those diagnostics are emitted through the flow analysis reporter in:

- [`clang/lib/Sema/AnalysisBasedWarnings.cpp#L2934`](/data/users/cssmith/git/llvm-nullsafe/clang/lib/Sema/AnalysisBasedWarnings.cpp#L2934)

### 4. Work for C and C++, with some ObjC coverage

The test coverage includes:

- C: [`clang/test/Sema/flow-nullability-c.c`](/data/users/cssmith/git/llvm-nullsafe/clang/test/Sema/flow-nullability-c.c)
- C++ core analysis: [`clang/test/SemaCXX/flow-nullability-analysis.cpp`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaCXX/flow-nullability-analysis.cpp)
- adoption/configuration: [`clang/test/SemaCXX/flow-nullability-adoption.cpp`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaCXX/flow-nullability-adoption.cpp)
- C++ language features: [`clang/test/SemaCXX/flow-nullability-cxx-features.cpp`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaCXX/flow-nullability-cxx-features.cpp)
- ObjC smoke test: [`clang/test/SemaObjC/flow-nullability-objc.m`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaObjC/flow-nullability-objc.m)

### 5. Support gradual rollout on unannotated code

Under:

- `-fnullability-default=nullable`, unannotated pointers are treated conservatively as nullable
- `-fnullability-default=nonnull`, unannotated pointers are treated ergonomically as nonnull unless explicitly marked nullable

That makes it usable both as:

- a migration tool for legacy code
- an annotation-driven checker for already-annotated code

### 6. Handle some patterns that are awkward in external tools

The upstream test suite explicitly covers:

- alias chains
- `__builtin_expect`
- `__builtin_assume`
- assertion-style macros that terminate

See:

- [`clang/test/SemaCXX/flow-nullability-crubit-regression.cpp`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaCXX/flow-nullability-crubit-regression.cpp)

## What It Cannot Do

### 1. It is intraprocedural

It reasons within one function body at a time.

It does not analyze callees interprocedurally. Cross-function contracts still have to come from annotations on signatures.

### 2. It does not do SAT-style disjunctive reasoning

This is the most important permanent gap relative to Crubit.

Example:

```c
if (!p1 && !p2) return;
if (p1)
  *p1;
else
  *p2;   // still warns here
```

Crubit can prove the `else` branch implies `p2` is non-null. This pass intentionally does not.

That limitation is documented directly in:

- [`clang/test/SemaCXX/flow-nullability-crubit-regression.cpp#L573`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaCXX/flow-nullability-crubit-regression.cpp#L573)

### 3. It is conservative around some C++ constructs

Known gaps or limitations in tests include:

- lambda bodies are analyzed separately, so narrowing from the outer scope does not carry into captures
- structured bindings are not tracked as precisely as ordinary local variables
- some loop facts are intentionally not inferred if they require stronger reasoning about trip counts

Relevant tests:

- [`clang/test/SemaCXX/flow-nullability-cxx-features.cpp#L166`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaCXX/flow-nullability-cxx-features.cpp#L166)
- [`clang/test/SemaCXX/flow-nullability-cxx-features.cpp#L442`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaCXX/flow-nullability-cxx-features.cpp#L442)
- [`clang/test/SemaCXX/flow-nullability-crubit-regression.cpp#L268`](/data/users/cssmith/git/llvm-nullsafe/clang/test/SemaCXX/flow-nullability-crubit-regression.cpp#L268)

### 4. It is not a general memory-safety system

It does not try to catch:

- use-after-free
- buffer overflows
- lifetime bugs in general
- ownership issues in general

It is specifically a nullability warning pass.

### 5. It is not full nullability inference

It does not try to infer and rewrite annotations across a codebase. It consumes existing annotations and defaults, then checks local behavior.

## Comparison To Stock Clang

The simplest positioning:

- stock Clang nullability is mostly type-based
- `nullsafe-upstream` adds flow-sensitive behavior

In practice that means stock Clang can warn about some type conversions, but misses many obvious unchecked dereferences of `_Nullable` pointers. This branch fills that gap inside the compiler's standard warning pipeline.

It also suppresses the older type-based nullable-to-nonnull conversion warning when the flow-sensitive checker is enabled, because the flow-based result is more precise:

- [`clang/lib/Sema/Sema.cpp#L690`](/data/users/cssmith/git/llvm-nullsafe/clang/lib/Sema/Sema.cpp#L690)

## Comparison To Crubit

The cleanest framing is:

- this branch upstreams a focused compiler warning pass
- Crubit has a broader nullability toolchain

### What this branch is relative to Crubit

It is closest to Crubit's nullability verification work, not to Crubit as a whole.

Crubit explicitly contains:

- nullability verification
- nullability inference
- Rust/C++ interop work that uses nullability information

See:

- [`../crubit/nullability/README.md`](/data/users/cssmith/git/crubit/nullability/README.md)

### Where this branch is stronger

- integrated directly into Clang warnings
- designed for normal compile-time use
- easier LLVM-upstream story than a separate clang-tidy-style tool
- naturally benefits from Clang CFG-based handling of constructs like `__builtin_expect`, abort/assert macros, and standard warning infrastructure

### Where Crubit is stronger

- broader scope: inference plus verification, not just checking
- richer reasoning in some cases, especially disjunctive proofs
- already has a larger, more mature nullability subsystem under `../crubit/nullability`

Crubit also has a much larger dedicated test corpus:

- [`../crubit/nullability/test`](/data/users/cssmith/git/crubit/nullability/test)

### The most accurate one-line comparison

`nullsafe-upstream` is best understood as "Clang-native, upstream-oriented flow-sensitive nullability warnings", while Crubit is "a larger nullability platform that includes verification, inference, and Rust-interop motivations."

## Good Ways To Describe The Upstream PR

### One sentence

"This adds an opt-in flow-sensitive nullability warning pass to Clang, so the compiler can warn on unchecked nullable-pointer uses based on local control flow rather than only on declared types."

### Thirty-second version

"The upstream branch is a focused Clang change: new flags, new warning groups, and a new intraprocedural dataflow analysis that tracks when nullable pointers have been proven non-null. It catches unchecked dereferences and related contract violations during normal compilation, supports gradual adoption, and ports a meaningful subset of Crubit's nullability-checking behavior into Clang proper."

### What to avoid saying

Avoid saying:

- "this solves null safety for C/C++"
- "this is equivalent to Crubit"
- "this does whole-program reasoning"
- "this infers annotations automatically"
- "this is a general memory-safety checker"

## If You Need To Explain The Broader Branch

The broader `nullsafe-clang-dev` branch is not just the upstream compiler patch. It also adds:

- extensive docs and architecture writeups
- playground/demo UX
- benchmark tooling and published performance results
- release/install automation
- nullability-annotated libc-style headers
- additional polish and follow-up fixes beyond the core upstreamable delta

That broader branch is the fork product. `nullsafe-upstream` is the LLVM-reviewable compiler core.
