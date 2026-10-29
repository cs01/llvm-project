# CIR modernize: proven source rewrites for C++ on ClangIR

Status: working prototype on branch `cir-dev` (local). Pointer parameter to
reference and pointer to const work per translation unit and project-wide,
and every applied edit is proven.

## Goal

Do for C++ what JSIR does for JavaScript: one IR that is high-level enough
to map back to source and low-level enough for dataflow, used to improve
existing code deterministically, with a proof for each change. We want to
make whole classes of errors impossible in existing code (null, reseating,
out-of-bounds, leaks) without moving it to another language.

The core idea: **prove on CIR, edit on the AST, certify by IR identity.**

- ClangIR (CIR) is MLIR with structured regions (`cir.scope`, `cir.if`,
  `cir.for`), so it keeps source structure *and* supports flow analysis.
- CIR is not designed to be lifted back to source: it drops macros, typedef
  spellings and templates as written. We do not lift it. Edits are built on
  the Clang AST, which is the most faithful C++ source model there is.
- Each edit is checked independently of CIR: classic codegen compiles the
  original and rewritten translation units, and their IR must match.

## Comparison

| | JSIR (Google) | This work |
|---|---|---|
| IR | MLIR dialect for JS | ClangIR (MLIR) |
| Dataflow | yes | yes (nullability facts today; MLIR dataflow framework next) |
| Back to source | lifts IR to AST | does not lift; edits the Clang AST using facts from CIR |
| Transforms | deobfuscation | hardening: ptr to ref today, then const, span, ownership |
| LLM | Gemini proposes, JSIR applies | planned: LLM proposes, CIR proves, IR check gates |
| Decompilation | Hermes bytecode to JS | out of scope |

Prior art for C++ hardening, as far as we know, is AST-level and unproven:

- Clang `-Wunsafe-buffer-usage` (C++ Safe Buffers, `UnsafeBufferUsage.cpp`)
  matches syntactic gadgets and emits `std::span` fix-its.
- Chromium's spanify rewriter runs per translation unit, emits edits plus
  dependency edges, and merges them across the codebase. Correctness rests on
  compiling, tests, review and runtime-checked spans.

What this adds: facts from dataflow instead of patterns (for example an
integer-range proof that an index stays in bounds), and a check for every
edit.

## Architecture

```
            per translation unit                      project.ts
 source --> Clang AST --> CIRGen --> CIR        pass 1: propose
                |                     |         pass 2: plan, edits, blockers
                |        CIRNullabilitySafety   merge: fixpoint over needs
                |          (param facts)        prove: IR identity per TU
                v                     v         apply: only if every TU proves
             CIRModernize: candidates, AST edits, blockers
```

### Facts (CIR)

`clang/lib/CIR/Dialect/Analysis/CIRNullabilitySafety.cpp` computes, per
pointer parameter: `mustDeref` (dereferenced before every way out, where any
call that might not return counts as a way out), `reassigned`,
`addressTaken`, `arithmetic`, `nullChecked`, `assignedNull`,
`callersPassNull`. Pointer to reference is sound when `mustDeref` holds and
none of the others do: a null argument was already undefined behavior, so
the reference adds no new UB.

### Edits (AST)

`clang/lib/CIR/FrontendAction/CIRModernize.cpp` turns a proven parameter into
edits: the declarator `*` in every redeclaration, `p->m` and `*p` in the
body, and each call site (`&x` to `x`, `p` to `*p`, `e` to `*(e)`). A
candidate is dropped as a whole as soon as any use falls outside the shapes
handled. Candidates that forward a parameter to one another stand or fall
together.

Blocking rules, each with a reason recorded for stats:

- Referenced from a template instantiation: the edit would land in the
  template, which other instantiations share. (Found by the checker: `isa<>`
  calls `To::classof(&V)`; with a converting constructor a missed call site
  still compiled and silently tested a temporary.)
- Named by a dependent call: unqualified calls block what their lookup found,
  plus free functions of that name when ADL applies; member and `T::` calls
  block methods of that name.
- Address taken, overloaded, virtual, special member, template, coroutine,
  variadic, default argument, captured by a lambda, declared in a macro or a
  system header.
- A call site passes a null constant, or has side effects in another
  argument, or decays an array.

### Project mode

Two cc1 flags, driven by `clang/utils/cirmodernize/project.ts`:

- `-fcir-modernize-export=<file>`: this TU's candidates, edits (file, byte
  offset, length, text), `needs` edges (keyed by USR and parameter index) and
  blockers, as JSON. Header declarations may be edited.
- `-fcir-modernize-plan=<file>`: functions proposed by other TUs. This TU
  reports the edits its own declarations and calls need, or why it blocks
  one. Inline functions in headers are only emitted, and so analyzed, where
  used; a TU with the body but no facts still produces the body edits, and
  the facts come from the plan.

Merge: a function survives if its defining TU still proves it, no TU blocks
it, and every `needs` edge survives (fixpoint). A TU that CIR cannot compile
blocks every planned function whose name appears in its preprocessed text.

Policy (driver): edits only under `--root`; no edits in `--public-headers`
directories unless `--allow-public-api`. The compilation database is taken to
be the whole program.

### Proof

`clang/utils/cirmodernize/lib.ts`, shared by `check.ts` (one TU) and
`project.ts`. Edits are mounted with `-ivfsoverlay`, so the tree is never
touched. Original and rewritten TUs are compiled at `-O0 -g0` with classic
codegen, and the IR must be identical after normalizing only what ptr to ref
is expected to change:

- mangled names (`P4Node` to `R4Node`), paired by order of first appearance,
  including inside other symbols (`@__func__.`, COMDATs, lambdas);
- `nonnull`, `align`, `dereferenceable` on signatures and calls, and the
  matching `!nonnull`/`!align` load metadata;
- struct type suffix numbers (`ArrayRef.1198`), renumbered per base name.

Gate audit: a wrapper clang that injects one wrong fix-it must make the check
fail. It does.

## Results (M4, Release + assertions)

| Run | Result |
|---|---|
| Support + IR, per TU (256 TUs) | 21 rewrites proven identical, 0 failed |
| CIR could not compile | 27 TUs before, 5 after fixing Darwin TLS wrappers and Apple ARM64 virtual member pointers |
| lib/IR, project mode, public headers excluded (85 TUs, 54s) | 19 rewritten, 21 TUs proven unchanged, 0 failed |
| Support + IR, per TU, ref and const | 76 rewrites proven identical, 0 failed |
| lib/IR, project mode, ref and const (85 TUs) | 70 rewritten, 27 TUs proven unchanged, 0 failed |

Why proven must-deref parameters are not rewritten (lib/IR, project mode):
276 dependent-call name, 83 unhandled body use, 54 overloaded, 34 forwarded
to a parameter that stays a pointer, 31 address taken, 17 public header.

## Roadmap

1. **Transform framework.** CIR patterns that produce AST edits and blockers
   through one API, instead of a hand-written modernizer per rewrite. The
   export, plan, merge and proof pipeline is already rewrite-agnostic.
2. **Generic dataflow on CIR.** MLIR's sparse constant propagation, integer
   ranges and dead code analyses run on any dialect whose control flow
   implements `RegionBranchOpInterface`; CIR's does (commit 12a993c fixed the
   loop exit edge for this). Add taint on top.
3. **Transforms.**
   - const-correctness: "never written through, never escapes to a writer".
     Same IR identity proof. Signature changes need project mode.
   - span: `(T *p, size_t n)` to `std::span<T>`. IR is not identical by
     design, so the proof becomes refinement: identical on every in-bounds
     execution, a trap instead of UB out of bounds (alive2-style). Integer
     ranges can prove some accesses in bounds, so their checks can go.
   - ownership: `new`/`delete` to `unique_ptr`, `lock`/`unlock` to
     `lock_guard`. Also refinement.
   - lifetime annotations: infer `[[clang::lifetimebound]]`,
     `[[clang::lifetime_capture_by(x)]]` and `[[clang::noescape]]` from CIR
     dataflow. Clang already has a modular, loan-based lifetime checker
     (`clang/lib/Analysis/LifetimeSafety`, `-Wlifetime-safety-*`) that checks
     each function against its annotated signature, so ownership needs no new
     proof system: the gate is no new lifetime-safety warnings anywhere in the
     project. Annotations do not change codegen, so IR identity still holds.
     Track the share of functions that fit the model; code outside it
     (pointer graphs, aliasing plus mutation such as iterator invalidation)
     stays unchecked.
   - bounds annotations: `-fexperimental-bounds-safety` and `counted_by` /
     `sized_by` are C only upstream, so for C++ the span rewrite is the
     bounds path, gated also by fewer `-Wunsafe-buffer-usage` warnings and
     run under libc++ hardening. For C translation units, infer
     `__counted_by(n)` where every access to `p[i]` is guarded by `i < n`;
     gate: builds under `-fexperimental-bounds-safety` and tests pass.
4. **Whole program at scale.** semdeps can limit re-proving to TUs that use a
   changed declaration.
5. **LLM proposes, CIR proves.** The generator can be wrong; the gate cannot.

Open design questions: how to run refinement proofs (alive2 needs an LLVM
built with RTTI and EH); preprocessor configurations (facts hold for one
build configuration; check several or refuse edits inside `#if`); templates
(a fact must hold for every instantiation, including future ones).

## Running it

```sh
B=build-cir.noindex
# one TU at a time
bun clang/utils/cirmodernize/check.ts --clang $B/bin/clang -p $B/compile_commands.json "**/llvm/lib/Support/*.cpp"
# drop reasons (assertions build)
bun clang/utils/cirmodernize/check.ts --clang $B/bin/clang -p $B/compile_commands.json --why "**/llvm/lib/IR/*.cpp"
# project-wide, dry run, then --apply
bun clang/utils/cirmodernize/project.ts --clang $B/bin/clang -p $B/compile_commands.json \
  --root $PWD/llvm --public-headers $PWD/llvm/include "**/llvm/lib/IR/*.cpp"
```

Tests: `clang/test/CIR/Analysis/modernize-param-reference.cpp`,
`clang/test/CIR/Analysis/modernize-project.cpp`.
