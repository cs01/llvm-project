# Safe C

**Write C that is as safe as Rust.**

Why rewrite C in a safe language when you can write C as a safe language?

Safe C is a **fork of Clang**. It adds one flag, `-fsafe-c`, that makes C memory safe:
every operation that could be undefined behavior is proven safe at compile time,
checked at run time with a trap, or rejected. You turn on one mode and stop thinking
about which of a dozen flags you forgot.

**Status: early.** Items marked planned below do not exist yet.

**Known holes.** An adversarial review on 2026-11-23 found programs that compile under
`-fsafe-c` without `__unsafe` and still corrupt memory. Most are now closed: null
pointers in zeroed slots, type confusion through unions and pointer casts, unchecked
`printf`/`scanf` arguments, the spoofable system-header exemption, and the flags and
attributes that switched checks off. Porting real code found more, now also closed:
integer-to-pointer casts, `memcpy` of raw bytes over pointers, uninitialized heap
memory, and Apple's libc annotations being off (so `strcpy` into a sized buffer
compiled). Still open: use after free and double free (ownership is designed and proved
in Lean, not built), the bytes `realloc` adds, `va_arg` of structs holding pointers, and
the size of the trusted base (system and legacy headers are trusted as declared). The
list: [safe-c-notes/review-2026-11-23.md](safe-c-notes/review-2026-11-23.md).

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
     become compile errors. The rules are proved sound in Lean
     ([OWNERSHIP.md](safe-c-lean/OWNERSHIP.md)); the compiler side is **planned**.
   - Not used: the static analyzer (a separate, unsound tool) and AddressSanitizer
     (run-time only, about 2x slower).
4. **Uninitialized reads**
   - Clang today: `-Wuninitialized` (a warning, misses cases).
   - Safe C **uses** clang's uninitialized-variables analysis, always on and as errors:
     reading a local that might not be set is a compile error. Arrays, structs and other
     locals the analysis does not track start zeroed (`-ftrivial-auto-var-init=zero`),
     so a read before the first write is a defined zero rather than undefined behavior.
     Heap memory from `malloc` starts zeroed too. **Works** (except the bytes `realloc`
     adds).
5. **Type confusion** (bad pointer casts, union punning, mismatched varargs)
   - Clang today: `-Wincompatible-pointer-types` and `-Wformat` (warnings).
   - Safe C **adds** the cast rule the Lean model needs: a pointer cast is allowed to
     `void *`, between identical types, or between two pointer-free types; `void *` to
     `T *` only from null or a fresh allocation. Union members with pointers follow the
     same rule. Integers cannot become pointers (except null and `void *`), and
     `memcpy`/`memset`/`read` into memory that holds pointers must copy the same type or
     write zero. **Works.**
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

## Safety features

| Bug | How Safe C stops it | From | Status |
|---|---|---|---|
| Out of bounds | `__counted_by`, `__sized_by`, `__ended_by` on interfaces; locals carry their own bounds; proven at compile time or trapped | Apple's `-fbounds-safety` | works |
| Null dereference | non-null by default, `_Nullable` must be checked before use | this fork | works |
| Weakening by flags | `-fno-bounds-safety` and friends rejected with `-fsafe-c` | this fork | works |
| Silencing by `-Wno-` or pragma | null and bounds violations are re-issued as errors that no flag or pragma can lower | this fork | works |
| Use after free, double free | owned pointers, borrows, `lifetimebound` at interfaces, named lifetimes where elision is not enough | this fork, on Clang's lifetime analysis | planned |
| Uninitialized reads | maybe-uninitialized locals are errors; aggregates and `malloc` memory start zeroed | clang's uninitialized analysis and `-ftrivial-auto-var-init` | works (not bytes `realloc` adds) |
| Type confusion | pointer casts only to `void *`, identical types or pointer-free to pointer-free; unions likewise; no integer-to-pointer; no raw bytes over pointers | this fork, rule from the Lean model | works |
| Unchecked libc | `strcpy`, `sprintf`, `gets` unavailable; a pointer with bounds cannot go to an unchecked (`__unsafe_indexable`) parameter | Apple's libc annotations, this fork | works |
| Lying `pure`/`const` | bodies are checked against the attribute | this fork | works |
| Integer UB | signed overflow wraps (as in Rust release builds); division by zero, `INT_MIN / -1` and oversized shifts trap | `-fwrapv` and Clang's trapping sanitizers | works |
| Calls into code not ported yet | `#pragma clang safe_c legacy` marks a header as trusted, one searchable line each | this fork | works (declarations trusted as written) |
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

**A machine-checked core.** [safe-c-lean/](safe-c-lean/) models Safe C's rules as a
small calculus in Lean 4 and proves that a well-typed program without `__unsafe` never
reaches undefined behavior: it finishes, traps or runs forever. The proof needs every
rule it relies on, and it found two rules that are not yet sound (type confusion through
casts, and the checked string conversion under aliasing); both are listed as open in the
[review notes](safe-c-notes/review-2026-11-23.md). The model covers the rules, not
clang's implementation of them; Juliet and the review probes cover that.

The ownership rules have their own proof (`own_sound`): a program the ownership checker
accepts never reaches use after free, double free or any other undefined behavior, and
each rule has a counterexample showing it is needed.

**Real code.** The second number is porting cost. Each port is in
[safe-c-notes/ports/](safe-c-notes/ports/), pristine first, so `git log -p` shows what
it took. The same source builds with Safe C, Apple clang and gcc.

| Library | Lines | What changed | `__unsafe` | Tests under Safe C |
|---|---|---|---|---|
| [jsmn](safe-c-notes/ports/jsmn/PORT.md) (JSON tokenizer) | 471 | 4 signatures annotated, no code changes | 0 (2 in its tests, for `va_arg`) | 16/16 |
| [cJSON](safe-c-notes/ports/cjson/PORT.md) (JSON parser and printer) | 3,512 | 448 lines, mostly `_Nullable` and bounds annotations; `sprintf`/`strcpy` to bounded forms | 3 (allocator hooks) | 18/18 |

Both ports found bugs in their test code (unchecked `malloc`, a buffer length that did
not match its buffer) and drove compiler fixes; cJSON's list of 13 Safe C gaps (things
it rejects or makes hard that are safe) is the usability to-do list. cJSON needs a small
update since the libc rules above landed (5 errors at its old workarounds).

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
