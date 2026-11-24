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
| Type confusion | done, except unions holding pointers (next) |
| Use after free | on by default; tested on two ported libraries |
| Data races | planned |

"Done" means checked at compile time or trapped, and passing NIST's Juliet suite. Two
small libraries are ported so far; larger ones are the real test.

Details: [how each is closed](safe-c-docs/protection.md).

## Results

<!-- generated:summary -->
On NIST's Juliet suite, bugs Rust rejects at compile time (null, double free, use after
free): 723 of 735 rejected at compile time. Bugs Rust catches at run time (bounds,
division by zero): 983 of 983 trapped, counting cases whose bug the test input reaches.
<!-- end generated:summary -->

Ports: [jsmn](safe-c-notes/ports/jsmn/PORT.md) and [cJSON](safe-c-notes/ports/cjson/PORT.md)
pass their tests under Safe C, Apple clang and gcc. More in [results](safe-c-docs/results.md).

## More

- [What you annotate](safe-c-docs/annotations.md), with the Rust equivalent of each
- [Examples](safe-c-docs/examples.md)
- [Adopting it one file at a time](safe-c-docs/adoption.md)
- [Results](safe-c-docs/results.md): Juliet, real code, the Lean proofs
- [Building](safe-c-docs/building.md)
- [Open issues](safe-c-notes/review-2026-11-23.md)

Apache License v2.0 with LLVM Exceptions, like LLVM.
