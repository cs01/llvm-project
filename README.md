# Safe C

**Why rewrite C in a safe language when you can write C as a safe language?**

Safe C is a mode of Clang, turned on with `-fsafe-c`, in which every operation that can
be undefined behavior is either proven safe at compile time, checked at run time with a
trap, or rejected. The goal is the guarantee safe Rust gives: no memory errors outside
code explicitly marked unsafe. The code is still C, compiled by a normal C compiler,
with the normal C ABI. The extra facts live in attributes on pointers.

This is an experimental fork. Most of it is not built yet; the status table below says
exactly what works today.

## Example

```c
#include <ptrcheck.h>
#include <stddef.h>

int sum(const int *__counted_by(n) a, size_t n) {
  int s = 0;
  for (size_t i = 0; i <= n; i++)  // off by one
    s += a[i];                     // traps at run time when i == n
  return s;
}

int get(int *_Nullable p) {
  return *p;                       // compile error: p may be null
}
```

```
$ clang -fsafe-c -c example.c
example.c:12:10: error: dereference of nullable pointer 'int *__single _Nullable' [-Werror,-Wnullability-safety-dereference]
```

## Rules

- **Safe by default.** An unannotated pointer is non-null and points to exactly one
  object. You annotate only where a pointer does more: `_Nullable`, `__counted_by(n)`,
  `__sized_by(n)`, `__ended_by(end)`. Pointers local to a function carry their own
  bounds and need no annotation.
- **Checked, not trusted.** Every access is proven in bounds at compile time, or checked
  at run time with a trap, the same way Rust checks slice indexing.
- **Not weakened by flags.** `-fsafe-c` cannot be combined with `-fno-bounds-safety`,
  `-fno-nullability-safety` or a nullability default other than `nonnull`. The only
  escape will be an `__unsafe` region written in the source, where reviewers can see it.

## Status

| Area | What it means | Status |
|---|---|---|
| `-fsafe-c` flag | turns on everything below, rejects flags that weaken it | works |
| Null safety | flow-sensitive check, non-null by default, possibly-null dereference is an error | works ([NULLSAFE.md](NULLSAFE.md)) |
| Bounds safety | `__counted_by` and friends, bounds-carrying local pointers, run-time traps | works (Apple's `-fbounds-safety`) |
| `__unsafe` regions | the one escape hatch | planned |
| Hard errors | safe-mode violations that no `-Wno-` flag or pragma can silence | planned |
| Initialization, casts, unions, varargs, signed overflow | rejected or trapped in safe code | planned |
| Calls to unannotated functions | allowed only from `__unsafe` | planned |
| Ownership and lifetimes | owned pointers, borrows, no use after free or double free | planned |
| Threads | no data races between threads | planned |
| Mechanical port to safe Rust | a check of the checker: safe C that ports to safe Rust passes rustc's borrow checker | planned |

## How it is built

- **Base:** Apple's [swiftlang/llvm-project](https://github.com/swiftlang/llvm-project)
  `next` branch, which is LLVM `main` plus Apple's patches, including the full
  `-fbounds-safety` implementation that is not upstream yet.
- **Null safety:** the [nullability-safety](https://github.com/cs01/llvm-project/tree/nullability-safety)
  fork, applied on top.
- **Safe C itself:** the `-fsafe-c` driver flag and the rules above, in this branch.

## Measuring it

Safety claims are scored against the [NIST Juliet C test suite](https://samate.nist.gov/SARD/test-suites/112),
which pairs a buggy `bad()` function with fixed `good()` versions for each kind of
bug (CWE). For each CWE, Safe C must reject or trap every `bad()` and accept every
`good()`. A segfault does not count as caught; only a compiler error or a
compiler-inserted trap does. The harness is `clang/utils/safe-c/juliet.ts`; notes and
results are in [safe-c-notes/juliet.md](safe-c-notes/juliet.md).

The second number is annotation burden: annotations per 1,000 lines on a real library,
and the share of its lines that need `__unsafe`.

## Build

```sh
cmake -G Ninja -S llvm -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64
ninja -C build clang
build/bin/clang -fsafe-c -isysroot $(xcrun --show-sdk-path) example.c
```

## Prior work

Safe C builds on ideas from Cyclone (fat pointers, regions, never-null pointers),
Checked C (checked pointer types and bounds declarations), Apple's `-fbounds-safety`,
Clang's lifetime safety analysis, and Rust. Fil-C takes a different route to the same
goal: no source changes, with run-time capabilities and a garbage collector.

## License

Apache License v2.0 with LLVM Exceptions, like LLVM. See [LICENSE.TXT](LICENSE.TXT).
