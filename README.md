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

## Get started

1. **Build the compiler** ([building](safe-c-docs/building.md)), then compile one file:
   `clang -fsafe-c parser.c`. Start with the file that reads untrusted input.
2. **Fix what it reports.** Each error says what is wrong and what to write.
3. **Most code needs four words:**

   | Write | When |
   |---|---|
   | `_Nullable` | the pointer may be null |
   | `__counted_by(n)` | the pointer has a length, `n` |
   | `_Owned` | whoever holds it must free it |
   | `_Excl` | the function changes what it points to |

4. **Not ready to fix a spot yet?** Put `#pragma clang safe_c legacy` at the top of a
   header to trust it as is, or wrap the code in `__unsafe { }`. Both are easy to find
   later.

Every annotation, with its Rust equivalent: [annotations](safe-c-docs/annotations.md).
Eight real bugs and their errors: [examples](safe-c-docs/examples.md).

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

## Results

The US government's test suite for C security tools (NIST's
[Juliet](https://samate.nist.gov/SARD/test-suites/112)) is thousands of small programs,
each with a known memory bug. A normal C compiler builds all of them without a word.
Safe C:

<!-- generated:summary -->
- **Null, double free, use after free: 608 of 608 caught at compile time.** Same as Rust.
- **Out of bounds, divide by zero: 100.0% stopped at the bad access** (5274 of 5274), where Rust would panic.

Counted only when Safe C gives a compile error or traps at the bug; on the 6501 cases
whose fixed version builds after porting. [Full table and the port](safe-c-docs/results.md).
<!-- end generated:summary -->

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
