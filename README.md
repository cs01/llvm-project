# Safe C

**Write C that is as safe as Rust.**

Why rewrite C in a safe language when you can write C *as* a safe language?

Safe C is a **fork of Clang** with one flag, `-fsafe-c`. It combines three things:

- **The newest safety features in Clang**, switched on together: pointers that carry
  their bounds (Apple's `-fbounds-safety`), nullability, lifetime annotations, the
  uninitialized-variable analysis, and trapping integer checks.
- **New checks where Clang has gaps**: flow-sensitive null checking, a pointer-cast rule,
  ownership and borrowing, checked `pure`/`const`/`alloc_size`, and `__unsafe` blocks.
- **A fixed configuration** that no flag, pragma or attribute can weaken.

**The goal:** code outside `__unsafe` is memory safe, as in Rust.

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
| Use after free, double free | Clang's lifetime analysis always on, as errors; `ownership_takes`/`ownership_returns`/`lifetimebound` at interfaces, checked against bodies | Clang's lifetime safety (usx95), this fork | local variables and annotated calls only; pointers stored in fields, arrays, globals or struct copies are not tracked |
| Uninitialized reads | a maybe-uninitialized local is an error; stack aggregates and `malloc` memory start zeroed | Clang's uninitialized analysis, `-ftrivial-auto-var-init` | works |
| Type confusion | pointer casts only to `void *`, to the same type, or between types without pointers; no cast removes `const`; bytes never become pointers or `_Bool` (`memcpy`, `read`, `arc4random_buf` included); unions likewise; no integer-to-pointer; `printf`/`scanf` checked, `%s` buffers checked for a terminator; `va_arg` only in `__unsafe`; a symbol declared with another type in another file does not link | this fork, rule from the Lean model | works |
| Unchecked libc | `strcpy`, `sprintf`, `gets` unavailable; a bounded pointer cannot go to an unchecked parameter; `FILE`/`DIR` handles are owned like memory; `setjmp`/`longjmp` only in `__unsafe` | Apple's libc annotations, this fork | works |
| Integer UB | signed overflow wraps (as in Rust release builds); division by zero and oversized shifts trap | `-fwrapv`, Clang's trapping sanitizers | works |
| Data races | thread-safety annotations, Send/Sync-style rules | `-Wthread-safety` | planned; a race on a pointer and its count breaks bounds checks today |
| Weakening | weakening flags rejected; safety errors cannot be lowered by `-Wno-` or pragmas; a file cannot declare itself a system header; lying `pure`/`const`/`alloc_size` rejected | this fork | works, except that `-isystem` marks any directory as trusted |
| Escape hatch | `__unsafe { }` blocks and functions; `#pragma clang safe_c legacy` for headers not yet ported; each is one searchable line, and `-Rsafe-c-unsafe` lists them all | this fork | works |

**Why it can be sound.** [safe-c-lean/](safe-c-lean/) is a Lean 4 model of Safe C's
proposed rules with machine-checked proofs: a program that follows them, outside
`__unsafe`, never reaches undefined behavior (it finishes, traps or runs forever), and the
ownership rules rule out use after free and double free. Every rule has a counterexample
showing the proof fails without it. The cast rule above comes straight from the model.
Clang implements the core rules; the ownership model (owned pointers, exclusive borrows,
no borrowed pointers in fields) is not built yet, and closing the gap between the model
and the compiler is the main work ahead.

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
// error: dereference of nullable pointer
*m = 1;
```

```c
int *m = malloc(sizeof(int));
if (!m) return -1;
*m = 1;  // ok: checked
```

### Bounds: wrong length at the call site

```c
int sum(const int *__counted_by(n) a,
        size_t n);

int x[3];
// error: passing array 'x' (which has
// 3 elements) ... with count value of 4
sum(x, 4);
sum(x, 3);  // ok
```

### Bounds: indexing a pointer that has no length

```c
// error: array subscript on single
// pointer 'p' must use a constant index
// of 0
void f(int *p) { p[1] = 0; }
```

```c
void f(int *__counted_by(n) p, size_t n) {
  if (n > 1)
    p[1] = 0;  // ok
}
```

### Bounds: off by one at run time

```c
int sum(const int *__counted_by(n) a,
        size_t n) {
  int s = 0;
  for (size_t i = 0; i <= n; i++)
    s += a[i];  // traps at i == n
  return s;
}
```

The trap is like a Rust panic.

### Use after free

```c
// error: object allocated by 'malloc'
// does not live long enough
int *p = malloc(sizeof(int));
free(p);   // note: freed here
*p = 1;    // note: later used here
```

This works for local variables and through calls annotated `__safec_owned` or
`__safec_takes`. Through struct fields, arrays and globals it is not caught yet (see the
status table).

## What you annotate

Most pointers need nothing: unannotated means non-null and one element, which is the
safe choice. You annotate only where a pointer does more:

<!-- generated:annotations -->
| Annotation | Meaning | In Rust | Example |
|---|---|---|---|
| *(nothing)* | non-null, exactly one element | `&T` | `void reset(struct conn *c);` |
| `_Nullable` | may be null; must be checked before use | `Option<&T>`: plain `&T` is never null | `char *_Nullable getenv(const char *name);` |
| `__counted_by(n)` | points to `n` elements; `n` is another parameter or field | `&[T]` (a slice: pointer plus length) | `int sum(const int *__counted_by(n) v, size_t n);` |
| `__counted_by_or_null(n)` | as `__counted_by(n)`, or null | `Option<&[T]>` | `struct buf { char *_Nullable __counted_by_or_null(len) data; size_t len; };` |
| `__sized_by(n)` | points to `n` bytes | `&[u8]` | `void fill(void *__sized_by(n) dst, size_t n);` |
| `__ended_by(end)` | valid up to the pointer `end` | a slice given as two pointers (`slice::from_ptr_range`) | `int scan(const char *__ended_by(end) p, const char *end);` |
| `__null_terminated` | a string ending in 0 (string literals and libc strings are already) | `&CStr` | `size_t my_strlen(const char *__null_terminated s);` |
| `__safec_takes(i)` | the function frees (or keeps) its `i`th parameter; callers may not use it afterwards | taking a `Box<T>` by value (a move) | `__safec_takes(1) void json_free(struct json *item);` (prefer `__safec_owned` on the parameter) |
| `__safec_owned` | on a parameter: the function takes ownership (frees it, or keeps it to free later); the caller may not use it afterwards | taking a `Box<T>` by value (a move) | `void json_delete(struct json *__safec_owned item);` |
| `__safec_returns_owned` | the result is new memory the caller must free | returning `Box<T>` | `__safec_returns_owned struct json *json_parse(const char *text);` |
| `__safec_lifetimebound` | on a parameter: the result points into it and dies with it | `fn f<'a>(x: &'a T) -> &'a U` | `const char *name_of(const struct json *j __safec_lifetimebound);` |
| `__safec_noescape` | on a parameter: the function does not keep the pointer | a plain `&T` parameter, which cannot outlive the call | `size_t count_spaces(const char *s __safec_noescape);` |
| `__unsafe` | a block or function where Safe C's rules are lifted, for code that cannot be checked | `unsafe { }` | `int first(va_list ap) { int v; __unsafe { v = va_arg(ap, int); } return v; }` |
| `#pragma clang safe_c legacy` | in a header not yet ported: its declarations are trusted, its code unchecked | an `extern "C"` block over unaudited C | first line of a header not yet ported |

The Rust column is the closest equivalent, not an exact one: a plain Safe C pointer can be
written through (like `&mut T`), and Rust's one-writer-at-a-time rule is not enforced yet;
exclusive borrows (`&mut T` proper) come with static ownership (M2).
<!-- end generated:annotations -->

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

**Results, split the way Rust would catch each bug** (2026-11-23):

<!-- generated:juliet -->
| CWE | Rust catches it at | Cases | Safe C at compile time | Safe C at run time (trap) | Missed, fixed input | Missed, needs other input |
|---|---|---|---|---|---|---|
| 476 NULL dereference | compile time | 162 | 162 (100.0%) | 0 (0.0%) | 0 | 0 |
| 690 unchecked return value | compile time | 399 | 399 (100.0%) | 0 (0.0%) | 0 | 0 |
| 415 double free | compile time | 120 | 108 (90.0%) | 0 (0.0%) | 12 | 0 |
| 416 use after free | compile time | 54 | 54 (100.0%) | 0 (0.0%) | 0 | 0 |
| 121 stack buffer overflow | run time | 315 | 0 (0.0%) | 288 (91.4%) | 0 | 27 |
| 122 heap buffer overflow | run time | 327 | 0 (0.0%) | 244 (74.6%) | 60* | 23 |
| 124 buffer underwrite | run time | 144 | 0 (0.0%) | 59 (41.0%) | 0 | 85 |
| 126 buffer over-read | run time | 144 | 0 (0.0%) | 118 (81.9%) | 0 | 26 |
| 127 buffer under-read | run time | 144 | 0 (0.0%) | 56 (38.9%) | 0 | 88 |
| 369 divide by zero | run time | 444 | 0 (0.0%) | 218 (49.1%) | 36* | 190 |

\* CWE 122: the 60 fixed-input misses are overflow only on 32-bit targets.
\* CWE 369: the 36 fixed-input misses are floating-point division, which is defined (infinity) in C and Rust.

**Bugs Rust rejects at compile time** (null, unchecked returns, double free, use after free): Safe C rejects 98.4% of them at compile time (723 of 735); the misses are double frees through function pointers and structs, the open struct-field gap.

**Bugs Rust catches at run time** (bounds, division by zero, where Rust panics): Safe C traps on every one whose bug actually happens with the harness's input (983 of 983). The other 439 need an input the harness does not give (a random value, a socket, a file), so a Rust program would not panic on them in this run either, and 96 are not bugs on this target (marked *).

Cases are `strict`: only those whose fixed `good()` version compiles and runs under Safe C. Generated from the saved runs in `safe-c-notes/` by `clang/utils/safe-c/gen-readme.ts`.
<!-- end generated:juliet -->

**Why not 100%.** In the table above, the only real misses are the 12 double frees,
where the pointer travels through a function pointer, a global or a struct before the
second free: that is the struct-field gap static ownership (M2) closes. Use after free
scores 100% because Juliet's cases flow through local variables; the audit probes through
fields and globals still get past until M2. Details and the harness's own fixes are in
[safe-c-notes/juliet.md](safe-c-notes/juliet.md).

**How Rust would compare** (by reasoning, not measured; we know of no Juliet run in safe
Rust): null, use after free, uninitialized reads and data races are compile errors in
Rust, soundly. Bounds are checked at run time in Rust too, so the random-index cases
above would go unseen there as well, and signed overflow wraps in Rust release builds as
it does here. Safe C matches Rust today on bounds, null, initialization and integers;
Rust is ahead on use after free (sound for every pointer, where Safe C covers locals and
annotated interfaces) and on data races, which Safe C does not address yet.

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
