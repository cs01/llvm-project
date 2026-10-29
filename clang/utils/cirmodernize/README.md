# CIR modernize

Proven source-to-source rewrites for C and C++, built on ClangIR.

CIR modernize finds code it can make safer, rewrites it, and proves each
rewrite does not change what the program does. One such rewrite, and the only
one implemented so far, turns a pointer parameter into a reference when every
path through the function already dereferences it:

```c++
// before                                   // after
static int first(Node *head) {              static int first(Node &head) {
  return head->value;                         return head.value;
}                                           }
int use(Node *n, Node local) {              int use(Node *n, Node local) {
  return first(n) + first(&local);            return first(*n) + first(local);
}                                           }
```

A null argument was already undefined behavior here, so the reference adds
no new restriction. It does remove, for good, a class of errors from that
function: it can no longer be passed null, reseated, or indexed past its
object.

## Use cases

- **Hardening existing code in place.** Make whole classes of errors
  impossible without moving the code to another language. A safety profile
  is reached by rewriting, not by rule-checking alone.
- **Large-scale changes people can trust.** Each edit comes with a check a
  reviewer does not have to redo: the rewritten translation units compile to
  the same LLVM IR.
- **A migration engine.** Nullability, ownership, const and bounds facts are
  what any move to a safer dialect or language needs first.

## What it can do today

- Pointer to reference for parameters that are dereferenced on every path,
  never reassigned, never null-checked, never used for arithmetic and never
  passed null.
- Within one translation unit (`check.ts`) or across a whole compilation
  database (`project.ts`), including declarations in headers and calls from
  other files.
- Prove every applied edit: original and rewritten code compile to identical
  LLVM IR at -O0 with classic codegen, apart from the reference attributes
  the rewrite is meant to add.
- Explain itself: every proven parameter it does not rewrite is counted by
  reason (`--why`).

## What it cannot do (yet)

- **Rewrites other than pointer to reference.** const, `std::span` and
  ownership (`unique_ptr`, `lock_guard`) are planned. Span and ownership
  change the IR on purpose, so they need a refinement proof, not identity.
- **Code ClangIR cannot compile.** Such files are skipped and block any
  function they might call. On LLVM's Support and IR libraries this is 5 of
  256 files.
- **Templates.** A fact has to hold for every instantiation, including ones
  not written yet, so templates and functions reached from them are left
  alone.
- **Multiple build configurations.** Facts hold for the configuration that
  was compiled. Code under other `#if` branches is not seen.
- **Callers outside the compilation database.** Project mode assumes it sees
  the whole program. Public API headers are excluded unless you allow them.
- **Lifting IR back to source.** CIR is not lifted. Edits are made on the
  Clang AST.

## How it works

ClangIR is an MLIR dialect that keeps C++ control structure as regions
(`cir.scope`, `cir.if`, `cir.for`). That makes it a good place for flow
analysis, but it drops what source rewriting needs: macros, typedef
spellings, templates as written. So the work is split:

1. **Prove on CIR.** A dataflow analysis computes per-parameter facts: is it
   dereferenced before every way out of the function (any call that might
   not return counts as a way out), reassigned, null-checked, used for
   arithmetic, passed null.
2. **Edit on the AST.** The Clang AST supplies every spelling that has to
   change: each declaration's `*`, each `p->m` and `*p`, each call site. A
   candidate is dropped whole if any use is a shape it does not handle, if
   its address is taken, or if a template or dependent call might reach it.
3. **Check independently.** The edits are mounted over the source with a VFS
   overlay, and the original and rewritten files are compiled with classic
   codegen, not CIR. The IR must match. The analysis that proposed an edit is
   never the one that checks it.

Project mode adds two passes over the compilation database. Each file first
proposes what it can prove. Then every file reports the edits its own
declarations and calls need, or why it blocks a function. A function is
rewritten only if its defining file proves it, no file blocks it, and every
function it forwards a parameter to is rewritten too.

See `docs/cir-modernize-design.md` for the design, results and roadmap.

## Quick start

Needs a clang built with `-DCLANG_ENABLE_CIR=ON` (and `llvm-cxxfilt` next to
it), a `compile_commands.json`, and [bun](https://bun.sh).

```sh
B=build/bin
# per file: list proven rewrites and check them
bun clang/utils/cirmodernize/check.ts --clang $B/clang -p build/compile_commands.json "**/src/*.cpp"

# whole project: propose, plan, merge, prove; add --apply to write the edits
bun clang/utils/cirmodernize/project.ts --clang $B/clang -p build/compile_commands.json \
  --root $PWD --public-headers $PWD/include
```

Or straight from the compiler, as warnings with fix-its:

```sh
clang++ -fclangir -S -emit-cir -fcir-modernize -o /dev/null file.cpp
```
