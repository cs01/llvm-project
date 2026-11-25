# Safe C

**Hardened C: Clang's bounds checking, null and type rules, mandatory.** An experiment.

A fork of Clang with one flag, `-fsafe-c`:

- **Bounds, null, uninitialized memory, casts, format strings and library calls** are
  checked: an error, or a trap at run time. This is what it aims to get right.
- **Use after free** is caught in common cases (a local freed and used again). Ownership
  annotations that catch more are off unless asked for
  (`-mllvm -safe-c-strict-ownership=true`), and are not sound.
- **Explicit boundaries:** `__unsafe`, legacy headers, `-isystem` directories. Anything
  Clang accepts that this has no rules for is an error.

It does not make C memory safe. See [Status](#status).

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
2. **Fix what it reports.** Most errors say what is wrong and what to write.
3. **The annotations you will meet first** (the cJSON port has about 630 annotations in
   3,300 lines, one every five lines; half are `_Nullable`, then
   `_Exclusive`, `_Owned` and `_Returns_owned`):

   | Write | When |
   |---|---|
   | `_Nullable` | the pointer may be null |
   | `__counted_by(n)` | the pointer has a length, `n` |
   | `_Owned` | whoever holds it must free it |
   | `_Exclusive` | the function changes what it points to |

4. **Not ready to fix a spot yet?** Put `#pragma clang safe_c legacy` at the top of a
   header to trust it as is, or wrap the code in `__unsafe { }`. Both are easy to find
   later.

Every annotation, with its Rust equivalent: [annotations](safe-c-docs/annotations.md).
Eight classic bugs, written as small examples, and their errors: [examples](safe-c-docs/examples.md).

## Status

| Problem | Status |
|---|---|
| Out of bounds | checked (compile time where provable, a trap otherwise) |
| Null dereference | checked |
| Uninitialized reads | locals must be set before use; heap memory is zero-filled, except what `realloc`, `reallocf` and `aligned_alloc` add (which may not hold pointers) |
| Integer UB | division by zero and oversized shifts trap; signed overflow wraps (as in Rust release builds) |
| Type confusion | unions holding pointers must be tagged unions, and every read checks the tag. A pointer to memory holding pointers may be a writable `void *` only in the call that needs one |
| Use after free, double free | on by default, **not sound**: see below |
| Data races | a thread owns its argument; locked data, mutable globals and signal handlers are checked. Not yet: a counted pointer and its count under one lock, shared owners (`Arc`) |

**Maturity.** Early. Tested on macOS (arm64) against Apple's SDK. Linux builds but is not usable yet
(glibc's headers have no annotations). Ported so far: jsmn, cJSON, lz4, inih. Some rule sets have small Lean models with
soundness proofs; they are not Clang, and Clang is not sound (see
[results](safe-c-docs/results.md)).
Every open item: [open issues](safe-c-notes/review-2026-11-23.md).

**Not sound, in any row.** On 2026-11-24 two independent attackers, each with only the user
docs, broke it: the first in 34 different ways, and after most of those were fixed, the
second in 32 more. The second found basic ones: a pointer moved into a field can still be
freed by its old name, pointers to `_Atomic` are not bounds-checked, and an ordinary
`char *` printed with `%s` is not scanned. All are listed in the
[open issues](safe-c-notes/review-2026-11-23.md), and the programs are in
`clang/utils/safe-c/holes`. Do not rely on it for security.

Details: [how each is closed](safe-c-docs/protection.md).

## Results

NIST's [Juliet](https://samate.nist.gov/SARD/test-suites/112) is the US government's
test suite for C security tools: thousands of small programs, each with a known bug and a
fixed version. Juliet is written for plain C, so it was ported first (the changes are
listed in [results](safe-c-docs/results.md)); 41% of its cases still do not build and are
not counted.

<!-- generated:summary -->
On a ported copy of NIST's Juliet suite, 6,773 of 11,522 cases in 10 bug classes (those whose fixed version builds after porting):

- **Null, double free, use after free: 608 of 608 rejected at compile time.** Rust also rejects these at compile time.
- **Out of bounds, divide by zero: 5,545 of 6,165 stopped at the bad access** (being rechecked: for 3,221 of them the harness could not rule out a trap raised by the platform). The other 620: 394 need an input the harness does not give (a random value, a file, a socket), and 226 are not bugs on this target (32-bit-only overflows, floating-point division, buffers that stay terminated).

A case counts only when Safe C gives a compile error or traps at the bug. [Full table, the port and the exclusions](safe-c-docs/results.md).
<!-- end generated:summary -->

Ports: [jsmn](safe-c-notes/ports/jsmn/PORT.md), [cJSON](safe-c-notes/ports/cjson/PORT.md)
and [lz4](safe-c-notes/ports/lz4/PORT.md) pass their tests under Safe C, Apple clang and
AddressSanitizer. lz4 runs at Apple clang's speed with its two hot loops marked audited
(`__unsafe _No_bounds_checks`), and at 0.77x (compress) and 0.58x (decompress) fully
checked.

## More

- [Safe C for Rust programmers](safe-c-docs/for-rust-programmers.md): the map, and real errors
- [The rules](safe-c-docs/rules.md): what is accepted, in one place
- [What you annotate](safe-c-docs/annotations.md), with the Rust equivalent of each
- [Examples](safe-c-docs/examples.md)
- [Adopting it one file at a time](safe-c-docs/adoption.md)
- [Results](safe-c-docs/results.md): Juliet, real code, the Lean proofs
- [Building](safe-c-docs/building.md)
- [Open issues](safe-c-notes/review-2026-11-23.md)

Apache License v2.0 with LLVM Exceptions, like LLVM.
