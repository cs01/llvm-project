# Safe C

**Write C that is as safe as Rust.**

Why rewrite C in a safe language when you can write C as a safe language?

Safe C is a **fork of Clang** with one flag, `-fsafe-c`. It combines three things:

- **The newest safety features in Clang**, switched on together: pointers that carry
  their bounds (Apple's `-fbounds-safety`), nullability, lifetime annotations, the
  uninitialized-variable analysis, and trapping integer checks.
- **New checks where Clang has gaps**: flow-sensitive null checking, a pointer-cast rule,
  ownership and borrowing, checked `pure`/`const`/`alloc_size`, and `__unsafe` blocks.
- **A fixed configuration** that no flag, pragma or attribute can weaken.

We listed every way C code can corrupt memory and closed each one, so **code outside
`__unsafe` is memory safe**: every operation that could be undefined behavior is proven
safe at compile time, checked at run time with a trap, or rejected. The core rules are
proved sound in Lean, and the compiler is tested against NIST's Juliet suite and real
libraries.

## What makes C unsafe

- **Out-of-bounds reads and writes**
- **Null dereference**
- **Use after free and double free**
- **Uninitialized reads**
- **Type confusion**: bad pointer casts, union punning, mismatched `printf` arguments
- **Integer undefined behavior**: signed overflow, oversized shifts, division by zero
- **Data races**
- **Escape hatches**: flags, pragmas, attributes and builtins that switch checks off

## How Safe C closes each one

| Problem | How Safe C stops it | Built on | Status |
|---|---|---|---|
| Out of bounds | `__counted_by`, `__sized_by`, `__ended_by` on interfaces; locals carry their own bounds; every access proven at compile time or trapped | Apple's `-fbounds-safety` | works |
| Null dereference | non-null by default; `_Nullable` must be checked before use, tracked through each function | this fork | works |
| Use after free, double free | owned pointers, moves and borrows; `lifetimebound` at interfaces | Clang's lifetime analysis, this fork | proved in Lean, compiler planned |
| Uninitialized reads | a maybe-uninitialized local is an error; stack aggregates and `malloc` memory start zeroed | Clang's uninitialized analysis, `-ftrivial-auto-var-init` | works |
| Type confusion | pointer casts only to `void *`, to the same type, or between types without pointers; unions likewise; no integer-to-pointer; no raw bytes over pointers; `printf`/`scanf` arguments checked | this fork, rule from the Lean model | works |
| Unchecked libc | `strcpy`, `sprintf`, `gets` unavailable; a bounded pointer cannot go to an unchecked parameter | Apple's libc annotations, this fork | works |
| Integer UB | signed overflow wraps (as in Rust release builds); division by zero and oversized shifts trap | `-fwrapv`, Clang's trapping sanitizers | works |
| Data races | thread-safety annotations, Send/Sync-style rules | `-Wthread-safety` | planned |
| Weakening | weakening flags rejected; safety errors cannot be lowered by `-Wno-` or pragmas; a file cannot declare itself a system header; lying `pure`/`const`/`alloc_size` rejected | this fork | works |
| Escape hatch | `__unsafe { }` blocks and functions; `#pragma clang safe_c legacy` for headers not yet ported; each is one searchable line, and `-Rsafe-c-unsafe` lists them all | this fork | works |

**Why it is sound.** [safe-c-lean/](safe-c-lean/) is a Lean 4 model of Safe C's rules
with machine-checked proofs: a program that follows the rules, outside `__unsafe`, never
reaches undefined behavior (it finishes, traps or runs forever), and the ownership rules
rule out use after free and double free. Every rule has a counterexample showing the
proof fails without it. The model is how several rules were found: the cast rule above
comes straight from it.

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

- **Calling code that is not safe yet.** Put `#pragma clang safe_c legacy` at the top of
  a header that is not ready. Safe C then treats it like a system header: its code is not
  checked and its declarations are trusted. The trust boundary is one searchable line per
  header, not one `__unsafe` per call, and `-Rsafe-c-unsafe` lists every legacy header in
  a build. The pragma is an error in the file being compiled (that file is always
  checked), and outside Safe C it does nothing. `#pragma clang system_header` in a user
  file is rejected, so this is the only way to declare one. Not yet done: requiring
  legacy declarations' pointers to be checked before use (today they are trusted as
  declared).
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

**The Lean core.** [safe-c-lean/](safe-c-lean/) models the rules as a small calculus.
`safe_c_sound` covers bounds, null, casts and integers; `own_sound` covers ownership.
Both are checked by Lean with no unproved steps. The model covers the rules, not clang's
implementation of them; Juliet, the ports and adversarial probes test that.

**Real code.** The second number is porting cost. Each port is in
[safe-c-notes/ports/](safe-c-notes/ports/), pristine first, so `git log -p` shows what
it took. The same source builds with Safe C, Apple clang and gcc.

| Library | Lines | What changed | `__unsafe` | Tests under Safe C |
|---|---|---|---|---|
| [jsmn](safe-c-notes/ports/jsmn/PORT.md) (JSON tokenizer) | 471 | 4 signatures annotated, no code changes | 0 (2 in its tests, for `va_arg`) | 16/16 |
| [cJSON](safe-c-notes/ports/cjson/PORT.md) (JSON parser and printer) | 3,512 | about 450 lines, mostly `_Nullable` and bounds annotations; `sprintf`/`strcpy` to bounded forms | 7 (2 allocator hooks, 5 string terminators) | 18/18 |

Both ports found bugs in their test code (unchecked `malloc`, a buffer length that did
not match its buffer) and drove compiler fixes; cJSON's list of 13 Safe C gaps (things
it rejects or makes hard that are safe) is the usability to-do list.

**Status.** Early, and moving fast. The compiler side of ownership and data races is
next. Open items, and everything found and fixed so far, are tracked in
[safe-c-notes/review-2026-11-23.md](safe-c-notes/review-2026-11-23.md).

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
