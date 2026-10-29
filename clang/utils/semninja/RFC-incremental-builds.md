# RFC: Faster incremental builds of LLVM (and any CMake + Ninja C++ project)

Status: draft, not posted.

## Overview

**Problem.** The slow part of LLVM's edit-build loop is recompiling. Ninja
decides what to rebuild from file timestamps, so touching a header recompiles
every file that includes it, even the ones that never use what changed. A
one-line edit to a common header like `Decl.h` recompiles 720 files and takes
over 4 minutes on a 10-core M4. Linking (about 1s) and CMake are not the
bottleneck.

**Approach.** Make the compiler report what each file actually used, then
rebuild only the files that used something that changed. Two smaller changes
make each compile cheaper on top of that:

| # | Change | What it does | Effect |
|---|---|---|---|
| 1 | CMake fix for AppleClang | Instantiates templates once in the precompiled header instead of in every file | Full build 30% faster |
| 2 | `-fcodegen-cache-path` | Skips the optimizer and code generator when a file's generated IR is unchanged | Warm cache: header-edit rebuilds 45 to 50% faster |
| 3 | `-fsemantic-deps` + `semninja` | Clang records the declarations, function bodies and macros each file used; a wrapper around ninja recompiles only files that used what changed | 3.7x to 4.5x on real upstream header edits |

**Result.** When a header edit touches something few files use, rebuilding
`clang` goes from about 3 minutes to under 1 minute. When most files really do
depend on the change, it costs a few seconds of analysis more than today.

**Correctness.** Every object these tools keep is byte-identical to a fresh
compile, checked in every benchmark and by a fuzzer (about 5000 random header
edits, 0 wrong skips). Whenever the analysis cannot prove a file is
unaffected, it falls back to recompiling it, which is today's behavior.

**Adoption.** Each change lands independently. #1 and #2 are small and
tested. #3 runs alongside the existing build: plain `ninja` still works and
agrees with `semninja` afterwards. Nothing changes for anyone who does not opt
in.

**Asks.** Review of #1 and #2 now; design feedback on #3.

## The three changes

Each is sound on its own and can land independently:

1. **CMake: enable `-fpch-instantiate-templates` for AppleClang.** CMake turns
   it on for Clang but not AppleClang (a version check from 2020; AppleClang
   has supported it since 12.0.5). One block in `HandleLLVMOptions.cmake`, plus
   a fix to CMake itself. Full build of `clang`: 645s to 450s (-30%). Output
   identical.
2. **`-fcodegen-cache-path=<dir>`: reuse the object when the generated IR is
   unchanged.** The key is a BLAKE3 hash of the module's bitcode (with use-list
   order), the typed cc1 invocation minus output names, and the compiler
   binary. A hit skips the optimizer and code generator. Sound by
   construction: the backend is a deterministic function of the key. Warm
   cache on header edits: about -45 to -50%.
3. **Semantic dependency tracking (`-fsemantic-deps` + `clang-semindex` +
   `semninja`).** Clang records which declarations, function bodies and macros
   each translation unit actually used. After a header edit, a ninja wrapper
   recompiles only the TUs that used something that changed, and marks the rest
   current in ninja's own logs, so plain `ninja` agrees afterwards.

## Results (M4, 10 cores, Release + assertions, AArch64 + X86)

Edits are real upstream commits, reverted and re-applied. Every kept object
was checked byte-for-byte against a fresh compile (`semninja --verify-last`).

| Commit (header edit) | plain ninja | semninja | speedup |
|---|---|---|---|
| getEnclosingFunction out of line | 191.3s | 42.5s | 4.5x |
| PointerUnion::dyn_cast migration | 190.6s | 52.0s | 3.7x |
| 25 headers, remove dead decls | 480.8s | 351.0s | 1.4x |
| FileID::getHashValue | 212.2s | 179.4s | 1.2x |
| Sema NFC (SpecialMemberCache) | 356.1s | 359.1s | 1.0x |

The last row is the expected worst case: a change every user of the header
really does depend on costs only the analysis (a few seconds).

## Correctness

* #1 and #2: the output is unchanged by construction; #2 has a lit test that
  also proves hits come from the cache.
* #3: kept objects are verified identical to fresh compiles in every benchmark
  above. A fuzzer applies about 5000 random and 36 targeted header mutations
  across seven TUs and checks every skip decision against a real recompile:
  0 wrong skips. Anything the analysis cannot attribute to a named entity
  (macros tested by conditionals, `#undef`, top-level tokens, declarations
  every includer emits) falls back to "recompile every dependent", which is
  what ninja does today.
* `-g` builds stay correct: with debug info the analysis treats a moved
  declaration as changed, since its line is in the object.

## What is not in this RFC

* Test-suite selection (run only tests that executed changed code): prototyped,
  deferred.
* Parallel backend within one TU: measured, about 1.5x on a single `.cpp` edit
  at a 5% runtime cost in the built compiler; not worth the complexity yet.
* macOS process startup: clang exports 5.9K weak symbols, so dyld does 62K
  coalescing lookups per launch (7.5ms of 12.5ms). 98% come from
  `DynamicRecursiveASTVisitorBase<bool>`; making its members strong cuts
  startup to 8.2ms and Sema+CodeGen lit wall time by about 8%. Separate patch.

## Asks

* CMake maintainers: accept the AppleClang flag fix upstream.
* Clang: review `-fcodegen-cache-path` (small, self-contained, tested).
* Feedback on #3's design before it is split into reviewable patches:
  `.sdeps` format, the AST reader hooks, and where the orchestrator should live
  (`clang/utils` vs a separate tool).
