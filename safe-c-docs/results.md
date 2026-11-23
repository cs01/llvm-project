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
| [cJSON](../safe-c-notes/ports/cjson/PORT.md) (JSON parser and printer) | 3,512 | about 500 lines: mostly `_Nullable` and bounds annotations, plus 26 `__safec_returns_owned`, 7 takes-ownership and 5 `lifetimebound`; `sprintf`/`strcpy` to bounded forms | 9 in the library (allocator hooks, string terminators, const casts) | 18/18 |

Both ports found bugs in their test code (unchecked `malloc`, a buffer length that did
not match its buffer) and drove compiler fixes; cJSON's list of 13 Safe C gaps (things
it rejects or makes hard that are safe) is the usability to-do list.

**Status.** Early, and moving fast. The compiler side of ownership and data races is
next. Open items, and everything found and fixed so far, are tracked in
[safe-c-notes/review-2026-11-23.md](../safe-c-notes/review-2026-11-23.md).
