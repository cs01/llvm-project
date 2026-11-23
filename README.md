# Safe C

**Write C that is as safe as Rust.**

Why rewrite C in a safe language when you can write C as a safe language?

C is unsafe because of seven kinds of bugs:

1. **Out-of-bounds access**: reading or writing past the end of a buffer.
2. **Null dereference**.
3. **Use after free**: dangling pointers, double free.
4. **Uninitialized reads**.
5. **Type confusion**: bad pointer casts, union punning, mismatched varargs.
6. **Integer undefined behavior**: signed overflow, oversized shifts, division by zero.
7. **Data races**.

Clang already has a feature for most of these: `-fbounds-safety` for bounds,
`lifetimebound` and `-Wlifetime-safety` for dangling pointers, `-Wuninitialized`,
trapping sanitizers for integer UB, `-Wthread-safety` for locks. But each is separate,
opt-in, and mostly a warning you can turn off.

This fork adds the missing pieces:

- **Null safety**: a flow-sensitive check that every possibly-null pointer is tested
  before use, with non-null as the default.
- **Ownership** (planned): owned pointers and borrow checking, so use after free and
  double free are compile errors.
- **`__unsafe` regions** (planned): the one escape hatch, visible in the source.

`clang -fsafe-c` turns all of it on and enforces it. Unannotated pointers get the
strictest meaning (non-null, exactly one element), violations are errors, and no
command-line flag can weaken it. What is left is C as safe as safe Rust: no memory
errors outside code marked `__unsafe`.

The code is still C. It compiles with the normal C ABI, links with any C library, and
the annotations are ordinary Clang attributes.

**Status: early.** Rows marked planned in the table below do not exist yet.

## Examples

### Null: unchecked `malloc`

```c
int *m = malloc(sizeof(int));
*m = 1;                         // error: dereference of nullable pointer
```

```c
int *m = malloc(sizeof(int));
if (!m) return -1;
*m = 1;                         // ok: checked
```

### Bounds: wrong length at the call site

```c
int sum(const int *__counted_by(n) a, size_t n);

int x[3];
sum(x, 4);                      // error: passing array 'x' (which has 3 elements)
                                //        ... with count value of 4
sum(x, 3);                      // ok
```

### Bounds: indexing a pointer that has no length

```c
void f(int *p) { p[1] = 0; }    // error: array subscript on single pointer 'p'
                                //        must use a constant index of 0
```

```c
void f(int *__counted_by(n) p, size_t n) {
  if (n > 1) p[1] = 0;          // ok
}
```

### Bounds: off by one at run time

```c
int sum(const int *__counted_by(n) a, size_t n) {
  int s = 0;
  for (size_t i = 0; i <= n; i++)
    s += a[i];                  // traps when i == n, like a Rust panic
  return s;
}
```

### Use after free (planned)

```c
int *p = malloc(sizeof(int));
free(p);
*p = 1;                         // will be: error: use of 'p' after it was freed
```

## Safety features

| Bug | How Safe C stops it | From | Status |
|---|---|---|---|
| Out of bounds | `__counted_by`, `__sized_by`, `__ended_by` on interfaces; locals carry their own bounds; proven at compile time or trapped | Apple's `-fbounds-safety` | works |
| Null dereference | non-null by default, `_Nullable` must be checked before use | this fork | works |
| Weakening by flags | `-fno-bounds-safety` and friends rejected with `-fsafe-c` | this fork | works |
| Silencing by `-Wno-` or pragma | null and bounds violations are re-issued as errors that no flag or pragma can lower | this fork | works |
| Use after free, double free | owned pointers, borrows, `lifetimebound` at interfaces | this fork, on Clang's lifetime analysis | planned |
| Uninitialized reads | definite initialization required | `-Wuninitialized`, made an error | planned |
| Type confusion | no pointer casts, no union punning, checked varargs | this fork | planned |
| Integer UB | signed overflow, shifts, division by zero trap | Clang's trapping sanitizers | planned |
| Calls into unannotated C | only from `__unsafe` | this fork | planned |
| Data races | thread-safety annotations, Send/Sync-style rules | `-Wthread-safety` | planned |
| Escape hatch | `__unsafe { }` regions | this fork | planned |

## What you annotate

Most pointers need nothing: unannotated means non-null and one element, which is the
safe choice. You annotate only where a pointer does more:

| Annotation | Meaning |
|---|---|
| `_Nullable` | may be null; must be checked before use |
| `__counted_by(n)` | points to `n` elements, `n` another parameter or field |
| `__sized_by(n)` | points to `n` bytes |
| `__ended_by(end)` | valid up to the pointer `end` |

`-fsafe-c` includes Apple's `ptrcheck.h`, which defines the bounds annotations, so
Safe C code does not need to include it.

## How it is checked

Claims are scored against the [NIST Juliet C test suite](https://samate.nist.gov/SARD/test-suites/112),
which pairs a buggy `bad()` function with fixed `good()` versions for each kind of bug.
Safe C must reject or trap every `bad()` and accept every `good()`. A segfault does not
count as caught: only a compiler error or a compiler-inserted trap does. Harness:
`clang/utils/safe-c/juliet.ts`; notes in [safe-c-notes/juliet.md](safe-c-notes/juliet.md).

A second number tracks annotation burden: annotations per 1,000 lines on a real library,
and the share of its lines that need `__unsafe`.

## How it is built

- **Base:** Apple's [swiftlang/llvm-project](https://github.com/swiftlang/llvm-project)
  `next` branch, which is LLVM `main` plus Apple's patches, including `-fbounds-safety`.
- **Null safety:** the [nullability-safety](https://github.com/cs01/llvm-project/tree/nullability-safety)
  fork, applied on top. Its own docs are in [NULLSAFE.md](NULLSAFE.md).
- **Safe C:** the `-fsafe-c` flag and the rules above.

```sh
cmake -G Ninja -S llvm -B build -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=ON \
  -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=AArch64
ninja -C build clang
build/bin/clang -fsafe-c -isysroot $(xcrun --show-sdk-path) example.c
```

## Prior work

Cyclone (fat pointers, regions, never-null pointers), Checked C (checked pointer types,
bounds declarations), Apple's `-fbounds-safety`, Clang's lifetime safety analysis, and
Rust. Fil-C reaches memory safety a different way: no source changes, run-time
capabilities and a garbage collector.

## License

Apache License v2.0 with LLVM Exceptions, like LLVM. See [LICENSE.TXT](LICENSE.TXT).
