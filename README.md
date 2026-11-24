# Safe C

**Write C that is as safe as Rust.**

Why rewrite C in a safe language when you can write C *as* a safe language?

A fork of Clang with one flag, `-fsafe-c`:

- Clang's newest safety features, all on.
- New checks where Clang has gaps.
- Nothing can switch them off.

**The goal:** code outside `__unsafe` is memory safe, as in Rust.

## What it looks like

```c
int sum(const int *__counted_by(n) v,
        size_t n);

_Returns_owned
struct json *parse(const char *text);

void json_delete(
    struct json *_Owned j);

int x[3];
// error: passing array 'x' (which has
// 3 elements) ... with count value of 4
sum(x, 4);

// error: object allocated by 'parse'
// does not live long enough
struct json *t = parse(text);
json_delete(t);  // note: freed here
t->kind;         // note: later used here
```

The same code compiles as plain C on any compiler: the annotations come from one header,
`safec.h`, and expand to nothing elsewhere.

## Status

| Problem | Status |
|---|---|
| Out of bounds | done |
| Null dereference | done |
| Uninitialized reads | done |
| Integer UB | done |
| Type confusion | done: unions holding pointers must be tagged unions, and every read checks the tag |
| Use after free | on by default; the 18 holes a fuzzing pass found are fixed, re-run pending |
| Data races | in progress: a thread owns its argument, locked data and signal handlers are checked; globals next |

"Done" means checked at compile time or trapped, and passing NIST's Juliet suite. Two
small libraries are ported so far; larger ones are the real test.

Details: [how each is closed](safe-c-docs/protection.md).

## Results: NIST's Juliet suite

[Juliet](https://samate.nist.gov/SARD/test-suites/112) is NIST's standard test suite for
C security tools: thousands of small programs, each with a real bug in a `bad()` function
and fixed `good()` versions, sorted by CWE (the standard list of weakness types). Static
analyzers and sanitizers are measured against it. A normal C compiler builds every one of
them without an error.

A case counts as caught only if Safe C stops it with a compile error, or with a trap at
the bad access when it runs (a crash or segfault does not count). The fixed versions must
still compile and run.

<!-- generated:summary -->
On NIST's Juliet suite, bugs Rust rejects at compile time (null, double free, use after
free): 498 of 498 rejected at compile time. Bugs Rust catches at run time (bounds,
division by zero): 1915 of 2017 trapped, counting cases whose bug the test input reaches.
<!-- end generated:summary -->

What this means: the memory bugs behind most C security fixes (use after free, double
free, null, out of bounds) are compile errors or a clean stop at the bad access, the same
split Rust makes. Scope: these are the 3011 cases whose fixed version builds under Safe C
as written. The rest need annotations first, mostly raw pointers in struct fields: that
is porting work, not missed bugs. Full table in [results](safe-c-docs/results.md).

Ports: [jsmn](safe-c-notes/ports/jsmn/PORT.md) and [cJSON](safe-c-notes/ports/cjson/PORT.md)
pass their tests under Safe C, Apple clang and gcc. More in [results](safe-c-docs/results.md).

## More

- [Safe C for Rust programmers](safe-c-docs/for-rust-programmers.md): the map, and real errors
- [What you annotate](safe-c-docs/annotations.md), with the Rust equivalent of each
- [Examples](safe-c-docs/examples.md)
- [Adopting it one file at a time](safe-c-docs/adoption.md)
- [Results](safe-c-docs/results.md): Juliet, real code, the Lean proofs
- [Building](safe-c-docs/building.md)
- [Open issues](safe-c-notes/review-2026-11-23.md)

Apache License v2.0 with LLVM Exceptions, like LLVM.
