# Results

[Back to the README](../README.md)

## How it is checked

Claims are scored against the [NIST Juliet C test suite](https://samate.nist.gov/SARD/test-suites/112),
which pairs a buggy `bad()` function with fixed `good()` versions for each kind of bug.
Safe C must reject or trap every `bad()` and accept every `good()`. A segfault does not
count as caught: only a compiler error or a compiler-inserted trap does. Harness:
`clang/utils/safe-c/juliet.ts`; notes in [safe-c-notes/juliet.md](../safe-c-notes/juliet.md).

**Results, split the way Rust would catch each bug** (2026-11-23):

<!-- generated:juliet -->
| CWE | Rust catches it at | Cases | Safe C at compile time | Safe C at run time (trap) | Missed, fixed input | Missed, needs other input |
|---|---|---|---|---|---|---|
| 476 NULL dereference | compile time | 80 | 80 (100.0%) | 0 (0.0%) | 0 | 0 |
| 690 unchecked return value | compile time | 342 | 342 (100.0%) | 0 (0.0%) | 0 | 0 |
| 415 double free | compile time | 114 | 114 (100.0%) | 0 (0.0%) | 0 | 0 |
| 416 use after free | compile time | 72 | 72 (100.0%) | 0 (0.0%) | 0 | 0 |
| 121 stack buffer overflow | run time | 2616 | 0 (0.0%) | 2541 (97.1%) | 0 | 75 |
| 122 heap buffer overflow | run time | 1149 | 0 (0.0%) | 1007 (87.6%) | 93* | 49 |
| 124 buffer underwrite | run time | 728 | 0 (0.0%) | 695 (95.4%) | 0 | 33 |
| 126 buffer over-read | run time | 560 | 0 (0.0%) | 420 (75.0%) | 102* | 38 |
| 127 buffer under-read | run time | 728 | 0 (0.0%) | 693 (95.1%) | 0 | 35 |
| 369 divide by zero | run time | 384 | 0 (0.0%) | 189 (49.2%) | 31* | 164 |

\* CWE 122: the 93 fixed-input misses are overflow only on 32-bit targets.
\* CWE 126: the 102 fixed-input misses are missing-terminator cases (CWE 170) that cannot over-read under Safe C: local arrays are zero-filled, so the copy stays terminated inside the buffer (Rust requires the same initialization).
\* CWE 369: the 31 fixed-input misses are floating-point division, which is defined (infinity) in C and Rust.

**Bugs Rust rejects at compile time** (null, unchecked returns, double free, use after free): Safe C rejects 100.0% of them at compile time (608 of 608).

**Bugs Rust catches at run time** (bounds, division by zero, where Rust panics): of the 5545 whose bug happens with the harness's input, Safe C traps on 5545 (100.0%); the other 0 get past it. 394 more need an input the harness does not give (a random value, a socket, a file), so a Rust program would not panic on them in this run either, and 226 are not bugs on this target (marked *).

Cases are `strict`: only those whose fixed `good()` version compiles and runs under Safe C after porting (below), 6773 of 11522. Of the rest, 1804 use a variable that is set only under a condition known at run time (on the other path it is uninitialized or still null), which Rust rejects too; the others still need porting work. Generated from `safe-c-notes/juliet-port6-2026-11-24.json` by `clang/utils/safe-c/gen-readme.ts`.
<!-- end generated:juliet -->

**How the suite was ported.** Juliet is written for plain C, so most fixed `good()`
versions are rejected as written. [`juliet-port.ts`](../clang/utils/safe-c/juliet-port.ts)
writes a ported copy (the original stays untouched), making the changes a person
porting the code would make. None removes a bug, and each was checked on cases where
the fixed version must run clean and the buggy one must still be caught:

- A pointer that starts as, or is set to, `NULL` is marked `_Nullable`.
- `strcpy`, `strcat`, `strncpy`, `strncat` and their wide versions become checked
  copies that trap on overflow ([`juliet_port.h`](../clang/utils/safe-c/juliet_port.h)).
  `strlcpy` would truncate, hiding the bug, so it is not used.
- A buffer passed to a helper, returned from one, or carried in a struct or union field
  carries its bounds (`__bidi_indexable`), as a Rust slice does.
- The unions and structs Juliet uses only to carry data between functions are `_View`.
- `charVoid`'s fields, which hold a string literal, are typed as static text instead of
  `void *`.

**How Rust would compare** (by reasoning, not measured; we know of no Juliet run in safe
Rust): null, use after free, uninitialized reads and data races are compile errors in
Rust, soundly. Bounds are checked at run time in Rust too, so the random-index cases
above would go unseen there as well, and signed overflow wraps in Rust release builds as
it does here. Safe C matches Rust today on bounds, null, initialization and integers;
On use after free, every pointer outside `__unsafe` is covered in both: Safe C requires every
pointer in a field or global to say whether it owns its target, and every parameter and
return to be a borrow, an owner or tied to a parameter. Both trust declarations they cannot
see into (Rust's `extern "C"`, Safe C's system and legacy headers). Rust is ahead in what
it can express safely: a struct can hold a borrow with a lifetime (`&'a T`), which Safe C
cannot yet, so back-pointers and graphs need `_Unchecked` (used only inside `__unsafe`). On
data races Safe C checks thread arguments, locked data, mutable globals and signal handlers,
but not yet that a counted pointer and its count share a lock, nor shared owners (`Arc`).

**The Lean core.** [safe-c-lean/](../safe-c-lean/) models the rules as a small calculus.
`safe_c_sound` covers bounds, null, casts and integers; `own_sound` covers ownership;
`tag_sound` covers tagged unions ([TAGGED.md](../safe-c-lean/TAGGED.md)) and `drop_sound`
`_Drop` cleanup ([DROP.md](../safe-c-lean/DROP.md)). Building each found a hole the compiler
then fixed: a tag test made a member look written after the tag changed, and a `_Drop_free`
variable could be given a pointer it did not own.
Both are checked by Lean with no unproved steps. The model covers the rules, not clang's
implementation of them; Juliet, the ports and adversarial probes test that.

**Real code.** The second number is porting cost. Each port is in
[safe-c-notes/ports/](../safe-c-notes/ports/), pristine first, so `git log -p` shows what
it took. The same source builds with Safe C, Apple clang and gcc.

| Library | Lines | What changed | `__unsafe` | Tests under Safe C |
|---|---|---|---|---|
| [jsmn](../safe-c-notes/ports/jsmn/PORT.md) (JSON tokenizer) | 471 | 4 signatures annotated, one `lifetimebound`, no code changes | 0 (7 in its tests, all `va_arg`) | 16/16 |
| [cJSON](../safe-c-notes/ports/cjson/PORT.md) (JSON parser and printer) | 3,512 | about 500 lines: mostly `_Nullable` and bounds annotations, plus 26 `_Returns_owned`, 7 takes-ownership and 5 `lifetimebound`; `sprintf`/`strcpy` to bounded forms | 9 in the library (allocator hooks, string terminators, const casts) | 18/18 |

Both ports found bugs in their test code (unchecked `malloc`, a buffer length that did
not match its buffer) and drove compiler fixes. Porting cJSON to the ownership rules took
51 `_Owned`, 85 `_Exclusive` and 22 `_Take`, raised its `__unsafe` count from 9 to 12 (4 of
them for the doubly linked list's back pointer, which Rust cannot check either), and
found a soundness hole and a crash in the compiler, both fixed.

**Status.** Early, and moving fast. Ownership is on by default; data races (M5) are
in progress. A fuzzing pass (random programs compiled under Safe C and, unchecked,
under AddressSanitizer) found 18 ways to get a memory error past the rules; the four
in tagged unions are fixed, the rest (views, borrows through getters and aliases,
`_Drop` copies) are being fixed, each with a test, and the fuzzer is rerun as the
gate. Open items, and everything found and fixed so far, are tracked in
[safe-c-notes/review-2026-11-23.md](../safe-c-notes/review-2026-11-23.md).
