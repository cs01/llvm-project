# Safe C

**Write C that is as safe as Rust.**

Why rewrite C in a safe language when you can write C as a safe language?

Safe C is a **fork of Clang**. It adds one flag, `-fsafe-c`, that makes C memory safe:
every operation that could be undefined behavior is proven safe at compile time,
checked at run time with a trap, or rejected. You turn on one mode and stop thinking
about which of a dozen flags you forgot.

**Status: early.** Items marked planned below do not exist yet.

**Known holes.** An adversarial review on 2026-11-23 found programs that compile under
`-fsafe-c` without `__unsafe` and still corrupt memory: null pointers in zeroed
pointer slots, type confusion through unions and pointer casts, unchecked `printf`/
`scanf` arguments, a spoofable system-header exemption, and several flags and
attributes that switch checks off. Until those are closed, Safe C is safer C, not safe
C. The list and its status: [safe-c-notes/review-2026-11-23.md](safe-c-notes/review-2026-11-23.md).

## Same code, any compiler

Safe C does not lock you in. Every annotation comes from one header, `safec.h`:

- **With the Safe C compiler** and `-fsafe-c`, the annotations are checked and the
  safety rules are enforced.
- **With any other C compiler** (GCC, MSVC, Xcode's clang, a stock Clang), `safec.h`
  defines every annotation as nothing. The same source compiles as ordinary C, exactly
  as it did before, just without the checks.

`safec.h` is a single file with no dependencies. The Safe C compiler ships it; for other
compilers, copy [`clang/lib/Headers/safec.h`](clang/lib/Headers/safec.h) into your project
(Apache 2.0 with LLVM exceptions, like the rest of LLVM). `-fsafe-c`
includes it automatically.

## What makes C unsafe, and what exists today

C has seven kinds of memory and undefined-behavior bugs. For each: what Clang has today,
what Safe C uses from it, and what Safe C adds.

1. **Out-of-bounds access**
   - Clang today: `-fsanitize=bounds` (run time, arrays of known size only),
     `-Warray-bounds` (a warning). The full `-fbounds-safety`, with pointers that carry
     their bounds, exists only in Apple's fork of Clang, not in Xcode's clang.
   - Safe C **uses** Apple's `-fbounds-safety` as is: interfaces declare bounds with
     `__counted_by(n)` and friends, every access is proven or trapped. **Works.**
2. **Null dereference**
   - Clang today: `_Nonnull` / `_Nullable` annotations and a few call-site warnings; no
     tracking of null checks through a function.
   - Safe C **uses** the same annotations and **adds** flow-sensitive null checking,
     with non-null as the default. **Works.**
3. **Use after free, double free, dangling pointers**
   - Clang today: the `lifetimebound` and `lifetime_capture_by` attributes, and the
     `-Wlifetime-safety` analysis that uses them to find dangling pointers (warnings,
     mostly for C++).
   - Safe C **uses** those attributes and builds on that analysis, and **adds** owned
     pointers (`_Owned`), moves, and borrow checking, so use after free and double free
     become compile errors. **Planned.**
   - Not used: the static analyzer (a separate, unsound tool) and AddressSanitizer
     (run-time only, about 2x slower).
4. **Uninitialized reads**
   - Clang today: `-Wuninitialized` (a warning, misses cases).
   - Safe C **uses** clang's uninitialized-variables analysis, always on and as errors:
     reading a local that might not be set is a compile error. Arrays, structs and other
     locals the analysis does not track start zeroed (`-ftrivial-auto-var-init=zero`),
     so a read before the first write is a defined zero rather than undefined behavior.
     **Works** (heap memory from `malloc` is not covered yet).
5. **Type confusion** (bad pointer casts, union punning, mismatched varargs)
   - Clang today: `-Wincompatible-pointer-types` and `-Wformat` (warnings).
   - Safe C **adds** a rule that converting `void *` to a typed pointer is allowed only
     inside `__unsafe`, except for null, fresh allocations (`malloc`, `calloc`,
     `realloc`) and byte views (`char *`). **Works.** Union punning and other pointer
     casts are **planned**.
6. **Integer undefined behavior** (signed overflow, oversized shifts, division by zero)
   - Clang today: `-fwrapv` (signed overflow wraps), and
     `-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero` with
     `-fsanitize-trap` (run-time traps). All off by default.
   - Safe C **uses** them the way Rust's release builds behave: signed overflow wraps
     (defined, not UB, nearly free); division by zero, `INT_MIN / -1` and oversized
     shifts trap (one compare, usually optimized away). A wrapped size or index that goes
     out of bounds is still caught by the bounds checks. For Rust's debug behavior, also
     trap on overflow with `-fsanitize=signed-integer-overflow -fsanitize-trap`
     (not yet combinable with `-fsafe-c`, which forces wrapping). Left-shifting a
     negative value is not trapped: Rust allows
     it, and `-fwrapv` code does not rely on it being undefined.
     **Works.**
7. **Data races**
   - Clang today: `-Wthread-safety` with lock annotations (warnings).
   - Safe C **uses** those annotations and **adds** rules for which data may be shared
     or moved between threads. **Planned.**

## Adopting Safe C, one file at a time

Nobody annotates a whole codebase at once. Safe C is adopted per file, and the code
builds and ships at every step.

- **The unit is a file.** `-fsafe-c` is a per-file flag. Safe files and plain C files link
  together unchanged: same ABI.
- **Four levels per file:**

  | Level | What it means | What you get |
  |---|---|---|
  | 0. Plain C | untouched | builds as before |
  | 1. Annotated interface | the header has `_Nullable`, `__counted_by`, ... | every safe caller is checked against it |
  | 2. Safe, with `__unsafe` | `-fsafe-c`, the hard spots wrapped in `__unsafe` | everything outside those blocks is checked |
  | 3. Fully safe | `-fsafe-c`, no `__unsafe` | the full guarantee |

- **Calling code that is not safe yet** (planned). Calling an unannotated function from
  safe code is an error unless its header is declared legacy, once per header with
  `#pragma clang safe_c legacy`. System headers count as legacy. The trust boundary is
  one searchable line per header, not one `__unsafe` per call. Pointers returned from
  legacy code carry no bounds, so they cannot be indexed without an explicit check.
- **Order:** code that handles untrusted input first (parsers, decoders, protocol
  handlers), where most memory-safety CVEs are; leaf utilities next; the core last.
- **Tooling** (planned): an annotator that proposes annotations from inference and
  compiler fix-its, each one checked by the compiler; a report of each file's level and
  `__unsafe` count; and a CI ratchet, so a file that reaches level 2 never goes back.

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
| Uninitialized reads | maybe-uninitialized locals are errors; aggregates start zeroed | clang's uninitialized analysis and `-ftrivial-auto-var-init` | works (not heap memory) |
| Type confusion | `void *` to `T *` only in `__unsafe` (except null, fresh allocations, byte views) | this fork | works; unions and other casts planned |
| Integer UB | signed overflow wraps (as in Rust release builds); division by zero, `INT_MIN / -1` and oversized shifts trap | `-fwrapv` and Clang's trapping sanitizers | works |
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

**First result, null safety only** (2026-11-23, Juliet unannotated): every `bad()` in
CWE 476 (NULL dereference, 270 cases) and CWE 690 (unchecked return value, 722 cases) is
rejected at compile time. 40-45% of the `good()` versions are also rejected as written,
because they keep NULL in places Safe C requires to be marked `_Nullable`; that is
annotation burden, and the annotated mode will measure it.

**Full run, all CWEs scored so far** (2026-11-23). `strict` counts only cases whose
`good()` compiles and runs, so a file-level error that rejects both says nothing:

| CWE | strict caught |
|---|---|
| 476 NULL dereference, 690 unchecked return | 100% (all at compile time) |
| 121 stack overflow | 93.3% |
| 126 buffer over-read | 84.0% |
| 127 buffer under-read | 83.3% |
| 124 buffer underwrite | 80.6% |
| 122 heap overflow | 72.8% (63 of the misses only overflow on 32-bit targets) |
| 369 integer divide by zero | every deterministically reachable case traps |
| 415 double free, 416 use after free | 0% (ownership not built yet) |

Almost every remaining bounds miss uses a random index or a randomly taken branch, which
no fixed input can trigger. Details and the harness's own fixes are in
[safe-c-notes/juliet.md](safe-c-notes/juliet.md).

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
