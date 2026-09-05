# C contracts for clang (`contracts-c-dev`)

> A branch of [cs01/llvm-project](https://github.com/cs01/llvm-project). It adds
> `-fc-contracts`: `pre` / `post` / `old()` for C as real grammar, checks them at
> compile time, and lowers them to CBMC so they can be *proved*. Upstream LLVM's
> README follows below.
>
> The fork's other line of work, flow-sensitive nullability, is independent and
> lives on
> [`nullsafe-clang-dev`](https://github.com/cs01/llvm-project/tree/nullsafe-clang-dev).

```c
unsigned long decompress(void *dst, unsigned long dstCap,
                         const void *src, unsigned long srcSize)
  pre  (dst != 0)
  pre  (dstCap > 0)
  post (r: r <= old(dstCap) || is_error((int)r));
```

`old(dstCap)` names the value at function entry, and it is required rather than
optional: a body may mutate its own parameter copy, so a `post` naming a bare
parameter would be ambiguous between entry and exit.

## Three tiers, all working

**Front end.** Clauses parse in the declarator suffix with the parameters in
scope, type-check, land in the AST, and survive a PCH. Predicates must be pure:
they may only call functions marked `const` or `pure`, which makes those
existing attributes the "usable in specs" marker rather than inventing one.

**Compile-time checking**, as an ordinary warning (`-Wcontract-violation`), from
a standalone CFG dataflow pass:

```
demo.c:16:12: warning: precondition cap > 0 of 'buf_new' is violated by this call
demo.c:3:3:   note: precondition declared here
```

It reports only preconditions it can show are *violated*, never ones it merely
cannot prove, and it assumes a callee's `post` after a call so `p = buf_new(8);
buf_put(b, ...)` is discharged rather than re-flagged.

**Proof.** `-fcontract-emit-cprover` lowers the same clauses to CBMC, nearly one
to one:

```
$ clang -fc-contracts -fcontract-emit-cprover decompress.c
__CPROVER_requires(dst != 0)
__CPROVER_ensures(__CPROVER_return_value <= __CPROVER_old(dstCap))
```

which `goto-instrument --enforce-contract` and `cbmc` then discharge.

## Results on real zstd

See **[proofs/zstd/](proofs/zstd/)**. Applied to the actual zstd sources, not a
transcription:

- **Two undefined-behaviour findings.** `ZSTD_overlapCopy8` forms a pointer up to
  8 bytes before the start of the output buffer, reachable with a legal stream
  (three-line fix, proved). `ZSTD_wildcopy` and `ZSTD_safecopy` subtract pointers
  their own doc-comments say are in different objects. Neither is exploitable on
  conventional hardware; both break CHERI and are freedoms a
  provenance-exploiting optimiser may take.
- **Functional correctness of the LZ reconstruction.** Not just "stays in
  bounds": `ZSTD_execSequence` provably emits the *right bytes*, including for
  matches that overlap their own output. This is the layer where "your data comes
  back" lives.
- **Two contract defects** where the real precondition is stronger than the
  documented one, or absent from where it is needed entirely.

None of the four is a crash, and **fuzzing cannot find any of them**, because
nothing misbehaves at runtime. That, rather than the count, is the argument.
A fifth check — the `FSE_readNCount` bounds audit — came back clean, and the
negative result is recorded too.

Read **[proofs/zstd/COST.md](proofs/zstd/COST.md)** before estimating anything:
solve time is driven by symbolic state size, not obligation count, and it decides
whether verifying a codec is a quarter or a research program.

## What is not done

`writes` is diagnosed as unimplemented. Loop invariants are designed but not
parsed. Proofs are bounded — exhaustive over a small domain rather than
universal; `proofs/zstd/UNBOUNDED.md` has the loop-contract route out and the
`do`/`while` blocker found along the way.

---

# The LLVM Compiler Infrastructure

[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/llvm/llvm-project/badge)](https://securityscorecards.dev/viewer/?uri=github.com/llvm/llvm-project)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8273/badge)](https://www.bestpractices.dev/projects/8273)
[![libc++](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml/badge.svg?branch=main&event=schedule)](https://github.com/llvm/llvm-project/actions/workflows/libcxx-pr-conformance-tests.yaml?query=event%3Aschedule)

Welcome to the LLVM project!

This repository contains the source code for LLVM, a toolkit for the
construction of highly optimized compilers, optimizers, and run-time
environments.

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and convert them into
object files. Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.

C-like languages use the [Clang](https://clang.llvm.org/) frontend. This
component compiles C, C++, Objective-C, and Objective-C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting in touch

Join the [LLVM Discourse forums](https://discourse.llvm.org/), [Discord
chat](https://discord.gg/xS7Z362),
[LLVM Office Hours](https://llvm.org/docs/GettingInvolved.html#office-hours) or
[Regular sync-ups](https://llvm.org/docs/GettingInvolved.html#online-sync-ups).

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.
