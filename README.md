# Safe C

**Write C that is as safe as Rust.**

Why rewrite C in a safe language when you can write C as a safe language?

Safe C is a **fork of Clang**. It adds one flag, `-fsafe-c`, that makes C memory safe:
every operation that could be undefined behavior is proven safe at compile time,
checked at run time with a trap, or rejected. You turn on one mode and stop thinking
about which of a dozen flags you forgot.

**Status: early.** Items marked planned below do not exist yet.

## Same code, any compiler

Safe C does not lock you in. Every annotation comes from one header, `safec.h`:

- **With the Safe C compiler** and `-fsafe-c`, the annotations are checked and the
  safety rules are enforced.
- **With any other C compiler** (GCC, MSVC, Xcode's clang, a stock Clang), `safec.h`
  defines every annotation as nothing. The same source compiles as ordinary C, exactly
  as it did before, just without the checks.

`safec.h` has no dependencies, so a project can copy it into its own tree. `-fsafe-c`
includes it automatically.

## What makes C unsafe, and what exists today

C has seven kinds of memory and undefined-behavior bugs. For each, here is what Clang
already offers and what this fork adds.

1. **Out-of-bounds access**
   - Upstream Clang: `-fsanitize=bounds` (run time, only arrays of known size),
     `-Warray-bounds` (warning), `counted_by` on struct fields. The `-fbounds-safety`
     flag is accepted but mostly not implemented yet.
   - Apple's fork of Clang: the full `-fbounds-safety`. Pointers carry their bounds,
     interfaces declare them with `__counted_by(n)` and friends, and every access is
     proven or trapped. Not in Xcode's clang.
   - Safe C: built on Apple's fork, so this **works**.
2. **Null dereference**
   - Upstream Clang: `_Nonnull` / `_Nullable` with a few warnings at call sites; no
     flow analysis.
   - This fork: flow-sensitive null checking, non-null by default. **Works**.
3. **Use after free, double free, dangling pointers**
   - Upstream Clang: `lifetimebound` and `-Wlifetime-safety` (warnings, mostly C++),
     the static analyzer (a separate tool), AddressSanitizer (run time, about 2x slower).
   - This fork: owned pointers and borrow checking. **Planned**.
4. **Uninitialized reads**
   - Upstream Clang: `-Wuninitialized` (warning, not sound), `-ftrivial-auto-var-init`
     (zeroes memory at run time).
   - This fork: definite initialization required. **Planned**.
5. **Type confusion** (bad pointer casts, union punning, mismatched varargs)
   - Upstream Clang: `-Wincompatible-pointer-types`, `-Wformat` (warnings).
   - This fork: casts and union punning only in `__unsafe`. **Planned**.
6. **Integer undefined behavior** (signed overflow, oversized shifts, division by zero)
   - Upstream Clang: `-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero`
     with `-fsanitize-trap` (run time traps).
   - This fork: turned on by `-fsafe-c`. **Planned**.
7. **Data races**
   - Upstream Clang: `-Wthread-safety` with lock annotations (warnings),
     ThreadSanitizer (run time).
   - This fork: Send/Sync-style rules. **Planned**.

## Why a fork and not a set of flags

Even where Clang has a feature, a set of flags does not give a guarantee:

- **Pieces are missing.** Bounds-carrying pointers are only in Apple's fork. Flow-sensitive
  null checking, ownership and `__unsafe` are only in this one.
- **Most of it is warnings.** Any of them can be turned off with `-Wno-...` or a pragma.
  Under `-fsafe-c` they are errors that no flag or pragma can lower.
- **Flags can be dropped.** One missing flag in one build file silently removes a check.
  `-fsafe-c` rejects the flags that would weaken it (`-fno-bounds-safety` and so on).
- **The defaults are wrong.** Upstream, an unannotated pointer means "unknown".
  Under `-fsafe-c` it means non-null and exactly one element, so forgetting an
  annotation is a compile error, not a hole.
- **The escape hatch must be visible.** The only way out is `__unsafe { }` in the
  source, where a reviewer can see and search for it.

## Standards and portability

The C standard committee is unlikely to adopt any of this soon, and it does not need to.
Compiler extensions are how C has always moved: GNU C, `__attribute__`, Apple's blocks,
Microsoft's SAL. Safe C code stays portable through `safec.h` (see [Same code, any
compiler](#same-code-any-compiler)).

The code is still C. It compiles with the normal C ABI and links with any C library.

## Examples

### Null: unchecked `malloc`

```c
int *m = malloc(sizeof(int));
*m = 1;                         // error: dereference of nullable pointer
```

```c
int *m = malloc(sizeof(int));
if (!m) return -1;
*m = 1;                         // ok: checked
```

### Bounds: wrong length at the call site

```c
int sum(const int *__counted_by(n) a, size_t n);

int x[3];
sum(x, 4);                      // error: passing array 'x' (which has 3 elements)
                                //        ... with count value of 4
sum(x, 3);                      // ok
```

### Bounds: indexing a pointer that has no length

```c
void f(int *p) { p[1] = 0; }    // error: array subscript on single pointer 'p'
                                //        must use a constant index of 0
```

```c
void f(int *__counted_by(n) p, size_t n) {
  if (n > 1) p[1] = 0;          // ok
}
```

### Bounds: off by one at run time

```c
int sum(const int *__counted_by(n) a, size_t n) {
  int s = 0;
  for (size_t i = 0; i <= n; i++)
    s += a[i];                  // traps when i == n, like a Rust panic
  return s;
}
```

### Use after free (planned)

```c
int *p = malloc(sizeof(int));
free(p);
*p = 1;                         // will be: error: use of 'p' after it was freed
```

## Safety features

| Bug | How Safe C stops it | From | Status |
|---|---|---|---|
| Out of bounds | `__counted_by`, `__sized_by`, `__ended_by` on interfaces; locals carry their own bounds; proven at compile time or trapped | Apple's `-fbounds-safety` | works |
| Null dereference | non-null by default, `_Nullable` must be checked before use | this fork | works |
| Weakening by flags | `-fno-bounds-safety` and friends rejected with `-fsafe-c` | this fork | works |
| Silencing by `-Wno-` or pragma | null and bounds violations are re-issued as errors that no flag or pragma can lower | this fork | works |
| Use after free, double free | owned pointers, borrows, `lifetimebound` at interfaces, named lifetimes where elision is not enough | this fork, on Clang's lifetime analysis | planned |
| Uninitialized reads | definite initialization required | `-Wuninitialized`, made an error | planned |
| Type confusion | no pointer casts, no union punning, checked varargs | this fork | planned |
| Integer UB | signed overflow, shifts, division by zero trap | Clang's trapping sanitizers | planned |
| Calls into unannotated C | only from `__unsafe` | this fork | planned |
| Data races | thread-safety annotations, Send/Sync-style rules | `-Wthread-safety` | planned |
| Escape hatch | `__unsafe { }` blocks and `__unsafe` functions lift Safe C's rules; bounds violations still need Apple's explicit `__unsafe_forge_*` forms | this fork | works |

## What you annotate

Most pointers need nothing: unannotated means non-null and one element, which is the
safe choice. You annotate only where a pointer does more:

| Annotation | Meaning |
|---|---|
| `_Nullable` | may be null; must be checked before use |
| `__counted_by(n)` | points to `n` elements, `n` another parameter or field |
| `__sized_by(n)` | points to `n` bytes |
| `__ended_by(end)` | valid up to the pointer `end` |

All of these come from `safec.h`, which `-fsafe-c` includes automatically, so
Safe C code does not need to include it.

## How it is checked

Claims are scored against the [NIST Juliet C test suite](https://samate.nist.gov/SARD/test-suites/112),
which pairs a buggy `bad()` function with fixed `good()` versions for each kind of bug.
Safe C must reject or trap every `bad()` and accept every `good()`. A segfault does not
count as caught: only a compiler error or a compiler-inserted trap does. Harness:
`clang/utils/safe-c/juliet.ts`; notes in [safe-c-notes/juliet.md](safe-c-notes/juliet.md).

A second number tracks annotation burden: annotations per 1,000 lines on a real library,
and the share of its lines that need `__unsafe`.

## How it is built

- **Base:** Apple's [swiftlang/llvm-project](https://github.com/swiftlang/llvm-project)
  `next` branch, which is LLVM `main` plus Apple's patches, including `-fbounds-safety`.
- **Null safety:** the [nullability-safety](https://github.com/cs01/llvm-project/tree/nullability-safety)
  fork, applied on top. Its own docs are in [NULLSAFE.md](NULLSAFE.md).
- **Safe C:** the `-fsafe-c` flag and the rules above.

```sh
cmake -G Ninja -S llvm -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64
ninja -C build clang
build/bin/clang -fsafe-c -isysroot $(xcrun --show-sdk-path) example.c
```

## Prior work

Cyclone and Checked C made C dialects; Fil-C makes C safe at run time with a garbage
collector. Safe C stays plain C, checked inside Clang: at compile time where it can be
proven, with a trap where it cannot.

## License

Apache License v2.0 with LLVM Exceptions, like LLVM. See [LICENSE.TXT](LICENSE.TXT).
