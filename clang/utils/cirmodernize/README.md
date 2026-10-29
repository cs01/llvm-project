# CIR modernize

Proven source-to-source rewrites for C and C++, built on ClangIR.

CIR modernize makes existing C++ safer in place. It finds code that can be
rewritten into a safer form, rewrites it in the source you own, and proves
each rewrite keeps what the program does. Whole classes of errors (null,
out-of-bounds, leaks, accidental writes) become impossible in that code,
without moving it to another language.

```c++
// before                                       // after
static int sum(Node *head, int *xs, int n) {    static int sum(const Node &head, std::span<const int> xs) {
  int s = head->value;                            int s = head.value;
  for (int i = 0; i < n; ++i)                     for (int x : xs)
    s += xs[i];                                     s += x;
  return s;                                       return s;
}                                               }
```

Each step here is a separate rewrite with its own proof: `head` is
dereferenced on every path and never written through, so it can be a const
reference; `xs` and `n` are only used as a bounded array, so they can be a
span, and every access is proven in bounds.

## Use cases

- **Hardening existing code.** Pointers that are never null become
  references, read-only pointers become const, pointer and length pairs
  become spans, owning raw pointers become `unique_ptr`, lock pairs become
  `lock_guard`. A safety profile is reached by rewriting the code, not only
  by flagging it.
- **Large-scale changes people can trust.** Every edit comes with a check a
  reviewer does not have to redo, so a change across thousands of files can
  land with the review effort of a small one.
- **A migration engine.** Nullability, ownership, const and bounds facts are
  what any move to a safer subset or a safer language needs first.
- **Analysis.** Taint tracking, constant propagation and integer ranges on
  ClangIR, for finding bugs as well as fixing them.
- **AI-assisted refactoring with a gate.** A language model proposes a
  rewrite; CIR modernize accepts it only if it can prove it. The generator
  can be wrong; the gate cannot.

## Design highlights

Source-to-source rewriting of C++ has two needs that pull in opposite
directions:

- The edits have to land in the source people own: macros, typedef
  spellings, templates as written, comments.
- Deciding an edit is safe takes dataflow: does every path dereference this
  pointer, can this index leave its bounds, who owns this allocation.

No single representation serves both. The Clang AST is the most faithful
model of C++ source there is, but flow analysis over it means modeling every
expression kind by hand. ClangIR is an MLIR dialect that keeps control
structure as regions (`cir.scope`, `cir.if`, `cir.for`) and spells out what
C++ leaves implicit (temporaries, conversions, constructors, cleanups,
exception edges), which makes it a good place for dataflow, but it is not
meant to be turned back into source.

So CIR modernize uses each for what it is good at:

1. **Prove on CIR.** Analyses compute facts about parameters, locals and
   allocations.
2. **Edit on the AST.** Rewrites turn facts into edits on the spellings that
   have to change. A candidate is dropped whole if any use is a shape it
   cannot rewrite, or if a template, a dependent call or a function pointer
   might reach it.
3. **Check every edit independently.** The rewritten files are compiled with
   classic codegen, not CIR, and compared with the originals. The analysis
   that proposed an edit is never the one that checks it.

## Rewrites

| Rewrite | Fact it needs | Proof |
|---|---|---|
| pointer parameter to reference | dereferenced on every path, never reassigned or passed null | IR identical |
| pointer to const pointee | never written through, directly or by a callee | IR identical |
| pointer and length to `std::span` | only used as a bounded array | refinement |
| owning raw pointer to `std::unique_ptr` | one owner, freed once on every path, never escapes | refinement |
| `lock`/`unlock` pair to `std::lock_guard` | paired on every path, including exceptions | refinement |
| add `lifetimebound` / `lifetime_capture_by` / `noescape` | returned or captured pointer derives from that parameter; never escapes (noescape) | IR identical; a new `-Wdangling*` / `-Wlifetime-safety-*` warning is a finding and holds the rewrite back |
| add `__counted_by(n)` (C only, deferred) | every access through the pointer is guarded by the count | builds under `-fexperimental-bounds-safety`, tests pass |
| out-parameter to return value | written on every path before any read | IR identical |

**IR identical**: the original and rewritten translation units compile to the
same LLVM IR, apart from what the rewrite is meant to change (mangled names,
reference attributes).

**Refinement**: the rewritten code does exactly what the original did on
every execution the original defined, and traps where the original had
undefined behavior (for example an out-of-bounds index). Integer ranges let
proven-in-bounds accesses skip their checks.

Each rewrite belongs to the group of its proof, `identical` or `refinement`.
`-fcir-modernize-only=` and `-fcir-modernize-skip=` (`--only` and `--skip` in
`check.ts` and `project.ts`) take rewrite names and group names, comma
separated. Without `--only` the default rewrites run: `ref` and `const`.
Naming a group turns on every rewrite in it, including the opt-in ones
(`nonnull`, `nullable`), and refinement rewrites run only when named.

## Analyses

All run on ClangIR: nullability and must-dereference, pointee writes,
ownership and lifetimes, integer ranges and constant propagation (MLIR's
dataflow framework), and taint.

## How it works

Per translation unit, the compiler runs the analyses, builds the candidate
edits, and reports them as warnings with fix-its, or applies them.

Across a codebase, two passes over the compilation database let signatures in
headers change with all their callers:

1. **Propose.** Each file proposes what it can prove about the functions it
   defines.
2. **Plan.** Each file reports the edits its own declarations and calls need
   for the proposals, or why it blocks one.
3. **Merge.** A function is rewritten only if its defining file proves it, no
   file blocks it, and every function it forwards an argument to is
   rewritten with it.
4. **Prove and apply.** Every affected file is compiled with all edits
   mounted over the source, and checked. Edits are written only if every file
   passes.

Public API headers are left alone unless you allow them. Only files that use
a changed declaration need checking again, so large codebases stay fast.

## What it does not do

- **Decompile.** It does not lift binaries or LLVM IR back to C++, and does
  not lift ClangIR back to source.
- **See past the compilation database.** It assumes the database is the whole
  program; callers elsewhere are not seen, which is why public headers are
  opt-in.
- **Reason about code it did not compile.** Facts hold for the build
  configurations that were compiled. Code under other `#if` branches is not
  analyzed, so a function is not rewritten when its body or any of its edits
  is inside an `#if` region (include guards aside), or when any translation
  unit skips a branch that names it. A partial run sees only the translation
  units that include a file under `--root` in this configuration.
- **Guarantee memory safety.** It removes error classes from the code it
  rewrites; code it cannot prove stays as it is. A memory-safe language makes
  promises about all code; this makes existing code safer, one proven
  rewrite at a time.

## Status

| Piece | State |
|---|---|
| pointer to reference, one file and project-wide | working, every edit proven (IR identical) |
| nullability and must-dereference facts | working |
| pointer to const pointee, one file and project-wide | working, every edit proven (IR identical) |
| span, unique_ptr, lock_guard, out-parameters | planned |
| lifetime annotations (`lifetimebound` on parameters, `this` and returned views; `lifetime_capture_by`), checked by clang LifetimeSafety | working, IR identical; new dangling warnings listed as findings |
| nullability qualifiers (`nonnull`, `nullable`, opt-in) | working; refused where `-pedantic` warns on them |
| refuse rewrites inside `#if` | working |
| proven clang-tidy fixes (`tidy.ts`) | working |
| `counted_by` inference for C | deferred (changes IR) |
| unsafe inventory report (every unsafe site, state and reason) | working (`--inventory`) |
| refinement proofs | planned |
| integer ranges, constant propagation, taint | planned |
| LLM proposals | planned |

Design, results and roadmap: `docs/cir-modernize-design.md`.

## Quick start

Needs a clang built with `-DCLANG_ENABLE_CIR=ON` (and `cir-modernize-ircmp`
next to it: `ninja clang cir-modernize-ircmp`), a `compile_commands.json`, and [bun](https://bun.sh).

```sh
B=build/bin
# per file: list proven rewrites and check them
bun clang/utils/cirmodernize/check.ts --clang $B/clang -p build/compile_commands.json "**/src/*.cpp"

# whole project: propose, plan, merge, prove; add --apply to write the edits
bun clang/utils/cirmodernize/project.ts --clang $B/clang -p build/compile_commands.json \
  --root $PWD --public-headers $PWD/include
```

clang-tidy's own fix-its, kept only where every TU that sees them compiles to
the same IR (`tidy.ts`, default checks `modernize-use-nullptr`,
`modernize-use-override`, `modernize-use-using`,
`google-readability-casting`; fixes inside `#if` regions are refused):

```sh
bun clang/utils/cirmodernize/tidy.ts --clang $B/clang --clang-tidy $(which clang-tidy) \
  -p build/compile_commands.json --root $PWD/lib "**/lib/*.cpp"
```

On LLVM's lib/IR it proved 165 of 173 fixes; one cast fix clang-tidy offered
on a function template's default argument did not compile, and the gate
dropped it.

Or straight from the compiler, as warnings with fix-its:

```sh
clang++ -fclangir -S -emit-cir -fcir-modernize -o /dev/null file.cpp
```
