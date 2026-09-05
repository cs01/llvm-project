# C contracts for clang (`contracts-c-dev`)

> This is a branch of [cs01/llvm-project](https://github.com/cs01/llvm-project),
> a fork of LLVM. It adds `-fc-contracts`: `pre` / `post` / `old()` clauses for C
> as real grammar, not macros and not comments. Upstream LLVM's README follows
> below.
>
> - **[contracts-design.md](contracts-design.md)** — the full design, the plan,
>   and what was cut and why.
> - **[contracts-example/](contracts-example/)** — `run.sh` demonstrates the
>   whole surface against a build of this branch.
> - The fork's other line of work, flow-sensitive nullability, lives on
>   [`nullsafe-clang-dev`](https://github.com/cs01/llvm-project/tree/nullsafe-clang-dev).

```c
unsigned long codec_decompress(void *dst, unsigned long dstCap,
                               const void *src, unsigned long srcSize)
  pre  (dst != 0)
  pre  (dstCap > 0)
  post (r: r <= old(dstCap) || codec_is_error((int)r));
```

Status: front end only. The clauses parse, type-check, land in the AST, and
survive a PCH. Nothing is checked yet, at runtime or at call sites.

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
