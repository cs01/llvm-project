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
| 476 NULL dereference | compile time | 84 | 84 (100.0%) | 0 (0.0%) | 0 | 0 |
| 690 unchecked return value | compile time | 234 | 234 (100.0%) | 0 (0.0%) | 0 | 0 |
| 415 double free | compile time | 108 | 108 (100.0%) | 0 (0.0%) | 0 | 0 |
| 416 use after free | compile time | 72 | 72 (100.0%) | 0 (0.0%) | 0 | 0 |
| 121 stack buffer overflow | run time | 562 | 0 (0.0%) | 496 (88.3%) | 34 | 32 |
| 122 heap buffer overflow | run time | 655 | 0 (0.0%) | 489 (74.7%) | 121* | 45 |
| 124 buffer underwrite | run time | 264 | 0 (0.0%) | 247 (93.6%) | 0 | 17 |
| 126 buffer over-read | run time | 336 | 0 (0.0%) | 236 (70.2%) | 68 | 32 |
| 127 buffer under-read | run time | 264 | 0 (0.0%) | 235 (89.0%) | 0 | 29 |
| 369 divide by zero | run time | 432 | 0 (0.0%) | 212 (49.1%) | 35* | 185 |

\* CWE 122: the 121 fixed-input misses are overflow only on 32-bit targets.
\* CWE 369: the 35 fixed-input misses are floating-point division, which is defined (infinity) in C and Rust.

**Bugs Rust rejects at compile time** (null, unchecked returns, double free, use after free): Safe C rejects 100.0% of them at compile time (498 of 498).

**Bugs Rust catches at run time** (bounds, division by zero, where Rust panics): of the 2017 whose bug happens with the harness's input, Safe C traps on 1915 (94.9%); the other 102 get past it. 340 more need an input the harness does not give (a random value, a socket, a file), so a Rust program would not panic on them in this run either, and 156 are not bugs on this target (marked *).

Cases are `strict`: only those whose fixed `good()` version compiles and runs under Safe C, 3011 of 11522. The rest are rejected as written, mostly because Juliet keeps raw pointers in struct fields and possibly-null values where Safe C wants them annotated: that is annotation work, not a bug found. Generated from the saved runs in `safe-c-notes/` by `clang/utils/safe-c/gen-readme.ts`.
<!-- end generated:juliet -->

**Why not 100%.** The 102 run-time misses are two gaps, both being fixed:
`wcscat` and the other wide-character string functions carry no bounds
annotations in the SDK, so Safe C does not check them yet (`strcpy` and `strcat`
are already unavailable); and a buffer without a terminator passed to a trusted,
unannotated function taking `const char *` (Juliet's own `printLine`) is read
past its end inside that function. The compile-time classes are at 100%. Details
and the harness's own fixes are in
[safe-c-notes/juliet.md](../safe-c-notes/juliet.md).

**How Rust would compare** (by reasoning, not measured; we know of no Juliet run in safe
Rust): null, use after free, uninitialized reads and data races are compile errors in
Rust, soundly. Bounds are checked at run time in Rust too, so the random-index cases
above would go unseen there as well, and signed overflow wraps in Rust release builds as
it does here. Safe C matches Rust today on bounds, null, initialization and integers;
Rust is ahead on use after free (sound for every pointer, where Safe C covers locals and
annotated interfaces) and on data races, which Safe C does not address yet.

**The Lean core.** [safe-c-lean/](../safe-c-lean/) models the rules as a small calculus.
`safe_c_sound` covers bounds, null, casts and integers; `own_sound` covers ownership.
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
51 `_Owned`, 85 `_Excl` and 22 `_Take`, raised its `__unsafe` count from 9 to 12 (4 of
them for the doubly linked list's back pointer, which Rust cannot check either), and
found a soundness hole and a crash in the compiler, both fixed.

**Status.** Early, and moving fast. Ownership is on by default; data races (M5) are
the last open class. Open items, and everything found and fixed so far, are tracked in
[safe-c-notes/review-2026-11-23.md](../safe-c-notes/review-2026-11-23.md).
