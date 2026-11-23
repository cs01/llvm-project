# Juliet gate

NIST Juliet C/C++ 1.3, C cases only. Harness: `clang/utils/safe-c/juliet.ts` (Bun).
Suite not vendored; set `JULIET_ROOT` to a dir holding `suite/C`.

```
JULIET_ROOT=~/git/ref/juliet bun clang/utils/safe-c/juliet.ts --cc build.noindex/bin/clang \
  --sysflags "-isysroot $(xcrun --show-sdk-path)" --flags "-fbounds-safety" \
  --cwe 121,122,476 --limit 30 --run --out /tmp/j.json
```

Scoring: bad is caught if it fails to compile or dies by SIGTRAP/SIGILL (a compiler
trap). SIGSEGV/SIGABRT count as missed: that is the UB happening to crash.
Good must compile, run, exit 0.

Gate audit (2026-11-22): system clang, CWE476, 20 cases: 0% caught, 100% good.
`-fsanitize=null -fsanitize-trap=null`: 95% caught, 100% good. So the gate moves both ways.

## Finding: unannotated Juliet does not compile under -fbounds-safety

2026-11-22, 30 cases each of CWE 121/122/476: 100% "rejected" but also 0% good accepted,
so the number is meaningless. Top errors: array-typed parameters decay to `__single`
(no arithmetic), passing arrays to unannotated `char *` parameters.

Decision (claude, 2026-11-22): score two modes.
1. As-is: compatibility only, never quoted as a safety number.
2. Annotated: each test file annotated once, mechanically (clang fix-its, then our
   inference), bad and good together since they share the source. Then good must pass
   and bad must still be caught. This is the headline number, and it also measures
   annotation burden (annotations added per case).

## Gate bug found 2026-11-23

The harness compiled test sources with `-w`, which also silences warnings promoted by
`-Werror=...`. A null-safety run scored 0% caught on CWE 476 and 690 even though the
compiler rejected the cases by hand. The earlier gate audit only exercised the run-time
trap path (`-fsanitize=null`), never compile-time rejection. Fixed: `-w` is now applied
to the support files only. Audit for the compile path: CWE 476, 20 cases, null safety
with `-Werror`: 100% rejected, 100% good accepted.

## First result: null safety alone, as-is (2026-11-23)

Flags: `-fnullability-safety -fnullability-default=nonnull -Werror=nullability-safety`,
no bounds safety, Juliet unannotated. Raw per-case data: `juliet-null-2026-11-23.json`.

| CWE | cases | bad caught | good accepted |
|---|---|---|---|
| 476 NULL dereference | 270 | 82.2% (all at compile time) | 77.8% |
| 690 unchecked return value | 722 | 78.9% (all at compile time) | 76.3% |

Misses are exactly eight Juliet flow variants (25 cases each): 45 and 68 (through a
global), 44 and 65 (through a function pointer), 32, 63, 64 (through `int **`), 66
(array of pointers). These are false negatives to fix.

Good cases rejected are mostly annotation burden: the good variants deliberately pass or
store NULL into functions and fields that check it, which under the nonnull default needs
`_Nullable` (144 argument, 50 field, 19 return errors). 18 are dereference errors to
look at as possible real false positives. The annotated mode will separate the two.

## Null pillar after one night of fixes (2026-11-23)

Same suite, null safety only (`-Xclang -fsafe-c` at cc1, no bounds safety), as-is.

| CWE | start of night | after globals | after fn-pointer args | after slot stores | after &p rule |
|---|---|---|---|---|---|
| 476 caught | 82.2% | 86.7% | 91.1% | 95.6% | **97.8%** |
| 690 caught | 78.9% | 84.2% | 89.5% | 94.7% | **97.4%** |

After the `void *` rule: **100%** caught on both (270 of 270, 722 of 722), all at
compile time; good accepted 60.0% (476) and 55.3% (690).

Only flow variant 64 is still missed (25 cases per CWE): the pointer is laundered through
`void *` and cast back. The Safe C `void *` rule closes it.

`good()` acceptance fell from about 77% to about 60% over the same fixes. That is
annotation burden, not a wrong analysis: the good variants keep NULL in globals, pass
`&p` to out-parameters and store NULL into pointer slots on purpose, and under Safe C
each of those needs a `_Nullable`. The annotated mode will report it separately.

## Integer rules: CWE 369 divide by zero (2026-11-23)

Flags: `-Xclang -fsafe-c -fwrapv -fsanitize(-trap)=integer-divide-by-zero,signed-integer-overflow,shift-exponent`
(what `-fsafe-c` turns on). The harness now feeds `0` on stdin, the value Juliet's
input-driven bad paths need; before that, fgets/fscanf cases never reached the bug.

| Family | Cases | Caught | Note |
|---|---|---|---|
| int, constant zero / fgets / fscanf, `/` and `%` | 228 | 224 (98.2%) | the 4 misses are flow variant 12, whose bad branch is taken only when `globalReturnsTrueOrFalse()` (random) says so |
| int, `rand()` divisor | 76 | not scored | the divisor is random; no input triggers the bug reliably |
| float | 152 | not scored | float division by zero is defined (IEEE: infinity), as in Rust |

Every deterministically reachable integer division by zero traps at run time.
CWE 190 (integer overflow) is not scored as a safety failure: under Safe C signed
overflow wraps, as in Rust release builds, and any out-of-bounds access it leads to is
caught by bounds safety.

## Bounds CWEs, first look (2026-11-23, 150-case samples, full `-fsafe-c`, unannotated)

| CWE | bad caught | good accepted |
|---|---|---|
| 121 stack overflow | 86.0% (121 trapped, 8 rejected) | 94.7% |
| 122 heap overflow | 98.7% (40 trapped, 108 rejected) | 20.0% |

Fixes that got here: `main`'s argv is counted by argc (every Juliet file failed in main
before); `void *` to `char *` byte views are allowed (bounds safety builds them
internally for memcpy); the harness feeds `100` (an out-of-range index) for the buffer
CWEs and `0` for 369.

121 misses: 18 of 21 are `CWE129_rand` (random index, not triggerable); the rest are the
random-branch variant.

122 good rejections are annotation burden plus one correct rule: unannotated `char *`
sink parameters passed to libc string functions (macOS headers declare them
`__null_terminated`), unannotated pointers passed to memcpy (`__sized_by`), unchecked
malloc results (CWE 690 itself). 12 good cases trap at run time: variants 21/34/42/67
route the buffer through a global, union, return value or struct field, which is
unannotated and so one element; the good code then indexes past it. Safe, but a
compile-time error naming the missing `__counted_by` would be better than a trap.

## Two more gate fixes (2026-11-23)

1. **Platform traps.** macOS malloc stops a double free with a trap instruction, the
   same SIGTRAP a compiler check raises. A first full run credited Safe C with 181
   "caught" CWE 415 cases it has no check for. Now every trapped bad case is rebuilt
   without the flags under test (`--baseline-cc`, default /usr/bin/clang) and run
   again; if the plain build traps the same way, the trap is the platform's and the
   case scores as missed.
2. **Strict score.** A file-level error (a shared helper, `main`, a type-confusion cast
   the good variant also makes) rejects bad() and good() alike, which says nothing about
   the bug. `strict%` counts only cases whose good() is accepted: of those, the share
   whose bad() is caught. `bad%` stays for comparison; `strict%` is the number to quote.

## Full run, strict score (2026-11-23, all cases, `-fsafe-c`, unannotated)

`strict%` = of the cases whose good() is accepted, the share whose bad() is caught.

| CWE | cases | good accepted | strict caught | notes |
|---|---|---|---|---|
| 121 stack overflow | 3960 | 315 | **93.3%** | misses: random index, random branch |
| 122 heap overflow | 2428 | 327 | **72.8%** | 63 of the misses are `sizeof(pointer)` bugs that only overflow on 32-bit targets (pointer size = element size on LP64) |
| 124 buffer underwrite | 1212 | 144 | **80.6%** (rerun with a negative index) | remaining misses: random index, random branch |
| 126 over-read | 896 | 144 | **84.0%** | |
| 127 under-read | 1212 | 144 | **83.3%** (rerun with a negative index) | remaining misses: random index, random branch |
| 415 double free | 228 | 186 | 0% | baseline: ownership is not built (M6) |
| 416 use after free | 138 | 36 | 0% | baseline: ownership is not built (M6) |
| 457 uninitialized read | 560 | 365 | 14.0% | baseline: definite initialization is not built (M3) |

Good acceptance is low (8-16%) on the bounds CWEs because Juliet as written breaks
Safe C rules on purpose in its good variants too: type-confusion casts from `void *`
(the CWE 135 family), `alloca` (whose result bounds safety marks unchecked), sink
parameters left unannotated and passed to annotated libc functions. That is the
annotation-burden question the annotated mode has to answer.

## CWE 457 uninitialized reads, after the definite-initialization rule (2026-11-23)

strict 20.9% (was 14.0%): uninitialized scalar locals are now compile errors. The misses:

- stack arrays and partly initialized structs (~127 cases): `-fsafe-c` zero-initializes
  locals the analysis does not track, so the read returns 0. No undefined behavior, but
  neither a trap nor an error, so Juliet counts them as missed. Safe as designed.
- arrays from `malloc` (~108 cases): heap memory is neither zeroed nor tracked. A real
  gap. Planned fix: under `-fsafe-c`, `malloc` allocates zeroed memory (as `calloc`).
