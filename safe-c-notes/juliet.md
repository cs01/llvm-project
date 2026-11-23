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
