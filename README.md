# Safe C

**Opt a C file in; what cannot be shown safe does not compile.** An experiment.

A fork of Clang with one flag, `-fsafe-c`:

- **Bounds, null, uninitialized memory, casts, format strings and library calls** are
  checked: an error, or a trap at run time. This is what it aims to get right.
- **Ownership** (use after free, double free) is checked on ClangIR, new and on by default: an
  `_Owned` pointer is moved by use, every other pointer is a borrow that dies when its owner
  changes, and a borrow is never stored. Code it has no rule for is an error:
  pointers to pointers, borrows stored in the heap. `-mllvm -safe-c-ownership=false` leaves it out for a file.
- **Explicit boundaries:** `__unsafe`, legacy headers, `-isystem` directories. Anything
  Clang accepts that this has no rules for is an error.

It does not make C memory safe. See [Status](#status).

## What it looks like

```c
int sum(const int *__counted_by(n) v,
        size_t n);

struct json *_Owned parse(
    const char *text);

void json_delete(
    struct json *_Owned j);

int x[3];
// error: passing array 'x' (which has
// 3 elements) ... with count value of 4
sum(x, 4);

struct json *_Owned t = parse(text);
json_delete(t);
// error: 't' is used after it was passed to
// 'json_delete' on line 10
t->kind;
```

The same code compiles as plain C on any compiler: the annotations come from one header,
`safec.h`, and expand to nothing elsewhere.

## Get started

1. **Build the compiler** ([building](safe-c-docs/building.md)), then compile one file:
   `clang -fsafe-c parser.c`. Start with the file that reads untrusted input.
2. **Fix what it reports.** Most errors say what is wrong and what to write.
3. **The annotations you will meet first** (the cJSON port has about 630 annotations in
   3,300 lines, one every five lines; half are `_Nullable`):

   | Write | When |
   |---|---|
   | `_Nullable` | the pointer may be null |
   | `__counted_by(n)` | the pointer has a length, `n` |
   | `_Owned` | whoever holds it must free it |
   | `_Take(place)` | move an owner out of a field, leaving null |
   | `_Trusted` | a function whose ownership is not checked inside; callers still are |

4. **Not ready to fix a spot yet?** Put `#pragma clang safe_c legacy` at the top of a
   header to trust it as is, or wrap the code in `__unsafe { }`. Both are easy to find
   later.

Every annotation, with its Rust equivalent: [annotations](safe-c-docs/annotations.md).
Twelve bugs as small examples, and their errors: [examples](safe-c-docs/examples.md).

## Status

| Problem | Status |
|---|---|
| Out of bounds | checked (compile time where provable, a trap otherwise) |
| Null dereference | checked |
| Uninitialized reads | locals must be set before use; heap memory is zero-filled, except what `realloc`, `reallocf` and `aligned_alloc` add (which may not hold pointers) |
| Integer UB | division by zero and oversized shifts trap; signed overflow wraps (as in Rust release builds) |
| Type confusion | a union's pointer members are used only in `__unsafe`; unions of plain data can be tagged and checked. Files that disagree about a shared type do not link. A pointer to memory holding pointers may be a writable `void *` only in the call that needs one |
| Use after free, double free | on by default; the rules are proved in Lean for a model, not for the compiler; attacked only as a prototype |
| Data races | only data marked `_Guarded_by` a lock |

**Maturity.** Early. Tested on macOS (arm64) against Apple's SDK. Linux builds but is not usable yet
(glibc's headers have no annotations). Ported: jsmn, inih, parson and cJSON with ownership on (cJSON's references are not sound), lz4 without. Some rule sets have small Lean models with
soundness proofs; they are not Clang, and Clang is not sound (see
[results](safe-c-docs/results.md)).
Every open item: [open issues](safe-c-notes/review-2026-11-23.md).

**Not sound, in any row.** Three blind attacks, by agents given only the user docs, found 34, then 32,
then 33 root causes; every program saved from the third is now rejected, and no fresh attack has been
run since. Do not rely on it for security. All of it is in the
[open issues](safe-c-notes/review-2026-11-23.md) and `clang/utils/safe-c/holes`.

**Cost.** On 45 small GitHub projects picked by hash (those with 200+ lines that build here), unmodified code has a median of
about 8 errors per 100 lines per project. Five real files were ported with no restructuring in four of them, at 0.99x to 1.28x
run time before the last checks were added ([details](safe-c-notes/realsize.md)).

Details: [how each is closed](safe-c-docs/protection.md).

## Results

NIST's [Juliet](https://samate.nist.gov/SARD/test-suites/112) is the US government's
test suite for C security tools: thousands of small programs, each with a known bug and a
fixed version. Juliet is written for plain C, so it was ported first (the changes are
listed in [results](safe-c-docs/results.md)); 41% of its cases still do not build and are
not counted.

Measured before the ownership checker was replaced (2026-11-25); not rerun since.

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
