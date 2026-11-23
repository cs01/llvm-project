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
