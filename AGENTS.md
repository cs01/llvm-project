# Working on Safe C with an agent

Safe C is a fork of Clang (LLVM) with one flag, `-fsafe-c`: C that does not compile, or stops at run time, if it could use memory
after freeing it, run off a buffer, follow a null pointer or read the wrong union member. Read [README.md](README.md) first, then
[safe-c-docs/rules.md](safe-c-docs/rules.md) (the rules) and [safe-c-docs/later.md](safe-c-docs/later.md) (what waits).

This file is for anyone pointing an agent at the repo. It says what is in scope, where the code is, and how a change is judged.
A change is judged only by the gates below, so read that part twice.

## What matters, in order

1. **Soundness.** A program the compiler accepts must not have a memory error the rules claim to stop. A change that makes the
   compiler accept something it rejected before is the riskiest change there is.
2. **Error messages.** Each error says what is wrong and what to write instead.
3. **Cost.** Run time 0 to 7% today; keep it there.

## Scope

| In scope | Out of scope |
|---|---|
| Fixing a hole (a program accepted with a memory error) | New language features or marks: the language is frozen ([rules.md](safe-c-docs/rules.md)) |
| Fixing a compiler crash | Anything in [later.md](safe-c-docs/later.md), unless a maintainer opens an issue for it |
| Better error messages | Threads beyond `_Guarded_by` |
| Wrappers in `safe-c-std` (no `__unsafe` inside) | C++ |
| Annotations for glibc and musl headers | Rewriting libc |
| Porting a small C library (see `safe-c-notes/ports`) | Changing upstream Clang behaviour outside `-fsafe-c` |
| Lean proofs of rules that exist (`safe-c-lean`) | Making tests pass by editing what they expect |
| The browser playground (`playground/`) | |
| Docs, when they say what the compiler does | |

A language change (a new mark, a rule that accepts more code) starts as an issue and waits for a maintainer. An agent never decides one.

## Where the code is

| Path | What |
|---|---|
| `clang/lib/Sema/SemaSafeC.cpp` | Type-level rules (bounds, null, casts, unions, library functions), and the table of every Safe C error message |
| `clang/lib/Sema/SafeCState.h` | Per-file state for those rules |
| `clang/lib/CIR/FrontendAction/SafeCOwnership.cpp` | Ownership: use after free, double free, leaks, borrows, on ClangIR |
| `clang/lib/Headers/safec.h` | The marks (`_Owned`, `_Nullable`, ...); `safe-c-std/safec.h` is a copy for other compilers and must stay identical |
| `safe-c-std/` | Safe library headers (`text.h`, `vec.h`, `json.h`, ...), programs written with them, misuse tests |
| `safe-c-lean/` | Lean 4 model of the rules and its soundness theorem |
| `clang/test/*/safe-c-*` | lit tests, one per rule |
| `clang/utils/safe-c/` | The gates: corpus, holes, CVE replay, libc check, docs check |

New errors go in the table in `SemaSafeC.cpp` and are issued with `S.SafeCDiag(Loc, "key")`. Do not add Safe C diagnostics to
`DiagnosticSemaKinds.td` or state to `Sema.h`: an edit there rebuilds most of Clang (10 minutes) where a table entry takes seconds.

## Build

```sh
cmake -G Ninja -S llvm -B build.noindex -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_PROJECTS=clang -DCLANG_ENABLE_CIR=ON -DLLVM_TARGETS_TO_BUILD="AArch64;X86"
ninja -C build.noindex clang FileCheck count not split-file llvm-lit
```

`-DCLANG_ENABLE_CIR=ON` is required: without it there is no ownership checker. The first build takes an hour or more; after that,
a change to `SemaSafeC.cpp` or `SafeCOwnership.cpp` rebuilds in under a minute. macOS (arm64) with Apple's SDK is where everything
is tested; Linux builds, but glibc's headers have no annotations yet.

## The gates

```sh
clang/utils/safe-c/check-all.sh          # every gate, one line each
clang/utils/safe-c/check-all.sh lit corpus   # just these
```

| Gate | What it proves |
|---|---|
| `lit` | Each rule's tests: the errors are exactly the expected ones |
| `corpus` | About 10,000 programs written to break Safe C. Fails if the compiler newly **accepts** a program and ASan finds a memory error in it (a hole), if it newly accepts one not signed off in `corpus/accepted.txt`, or if it crashes on a new one |
| `holes` | Every hole found so far stays closed |
| `cve` | 21 real bugs from the Magma benchmark (23 programs) stay blocked, and their fixed versions still compile and run; fails if fewer programs ran |
| `libc` | What each C99 library function is allowed to do |
| `misuse` | Wrong uses of the `safe-c-std` headers are rejected |
| `docs` | Every example in the docs gives the error it shows, word for word |
| `ports` | Ported libraries build and pass their own tests |
| `headers` | The two copies of `safec.h` match |
| `lean` | The Lean model builds, every file is imported, and no proof is skipped (`sorry`, `axiom`, ...) |

Run the narrowest gate while you work (one lit test: `build.noindex/bin/llvm-lit -v clang/test/Sema/safe-c-union.c`), and every
gate before you open a pull request.

For a change that accepts more code, also run `bun clang/utils/safe-c/corpus/check.ts --audit` (about 10 minutes): it builds
every accepted program with Safe C and ASan and must report no holes. It needs an ASan runtime whose ABI matches the fork
(Homebrew's LLVM, or `ASAN_LINK=path/to/clang`).

## Rules for a change

These exist because a gate that can be edited cannot be trusted.

- **Every new rejection has a lit test that fails on the compiler before your change.** Show both runs in the pull request.
- **Never change what a test expects in the same change as the checker**, unless the pull request explains each changed line.
- **Never edit `corpus/verdicts.txt` or `corpus/accepted.txt` by hand.** `check.ts --update` rewrites verdicts only when nothing
  was newly accepted. Newly accepted programs are signed off by a maintainer in `accepted.txt`.
- **A change that accepts more code needs a reason it is sound**: a Lean proof, or an argument in the pull request plus a corpus
  run. Expect it to be reviewed slowly.
- **Do not weaken a gate.** Changes to `.github/`, `clang/utils/safe-c/`, `safe-c-lean/` and `safe-c-docs/rules.md` are reviewed
  by a maintainer (CODEOWNERS).
- **Small pull requests**, one problem each. Say which gates you ran and paste their lines from `check-all.sh`.

## Finding work

- Issues labelled **`agent-ready`** have a description of done and the gate that shows it.
- **Crashes:** `corpus/check.ts` lists programs that crash the compiler. Each is a bug; fix one, add a lit test.
- **Holes:** write a program that `-fsafe-c` accepts and that has a memory error under ASan. Put it in an issue with the ASan
  output. A confirmed hole becomes a test in `clang/utils/safe-c/holes`. This is the most useful work there is.
