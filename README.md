# Safe C

**C that cannot use memory after freeing it, run off the end of a buffer or follow a null pointer, and still reads like C.** A research preview.

A fork of Clang with one flag, `-fsafe-c`, opted into one file at a time. What it cannot show is safe does not compile, or stops at
run time instead of corrupting memory. The marks come from one header, `safec.h`, and mean nothing to other compilers: the same file
is C99, and the ABI does not change.

It does not make every C program safe, and it is not proven sound: see [Limits](#limits).

## What it looks like

```c
#include "json.h"
#include "file.h"

int main(void) {
  struct text in = {0};
  struct json j = {0};
  if (!file_read("people.json", &in).ok || !json_parse(&j, text_str(&in)).ok)
    return 1;
  for (size_t p = json_first(&j, 0); p; p = json_at(&j, p).next) {
    struct str name = json_text(&j, json_get(&j, p, STR("name")));
    str_write(name, "\n", stdout);
  }
  json_free(&j);
  text_free(&in);
}
```

No marks at all: they are in the headers. What goes wrong is an error:

```c
struct str name = json_text(j, 1);
json_parse(j, text);
return name.p[0];
// error: 'name.p' is used after the call to 'json_parse' on line 4

struct json_value v = json_at(j, json_get(j, person, STR("age")));
return v.number;
// error: reading 'number' needs the tag 'kind' known to be JSON_NUMBER here, or the
// union's bytes could be another member's; check it first (as in 'if (x.kind == JSON_NUMBER)')
```

## Real bugs

Bugs from [Magma](https://hexhive.epfl.ch/magma/), a benchmark of real CVEs in libpng, libtiff, libsndfile, libxml2, sqlite and
OpenSSL, picked by rule and rewritten in Safe C by hand ([how](clang/utils/safe-c/cve/README.md)):

| Kind | Blocked | When | Stock Clang `-Wall -Wextra` |
|---|---|---|---|
| Use after free (every one in the sample) | 4 of 4 | compile time | no warning |
| Out of bounds, null, type confusion, divide by zero | 17 of 17 | 3 at compile time, 14 stop at run time | no warning |

The fixed version of each is accepted and runs. They are reductions of a few lines, not the libraries.

## The style

Seven programs were written this way: `wc`, `uniq`, `comm`, `sort` and `tsort` from [sbase](https://core.suckless.org/sbase/), a JSON
parser and a [text editor](safe-c-std/programs/edit.c) ([all of them](safe-c-std/programs)). The tools print what the originals print on 4,200 random inputs, the JSON
parser agrees with JavaScript's on 3,987 documents, and the editor with a model on 17,467 keys. Only the editor has unsafe code: three
small functions around the terminal.

| Instead of | Write |
|---|---|
| hoping every `malloc` is freed | nothing: an owner that is never freed, returned or handed on is an error |
| a pointer per node, freed one by one | one array of nodes and indexes into it (`VEC`): one thing to free |
| `char *` with a length somewhere else | `struct str` to look at text, `struct text` to own it |
| setting a pointer and then its length | `_Set(p, n, new_p, new_n)`, both at once |
| a union and a comment saying which member | `_Tagged_by(kind)` and `_When(...)` on each member |
| a pointer that may be null, and hoping | `_Nullable`, and the compiler makes you check |
| a parameter you only read | `const`: then nothing it points to changes in the call |
| returning `int` for success and hoping it is checked | `RESULT(name, T, E)`: reading the value before checking `ok` is an error; `TRY` returns the error early |

## The standard library

Not rewritten. The compiler knows what each C function frees, keeps and returns. Of 118 C99 functions, 98 are checked as they are
and 20 are errors that name a replacement (`clang/utils/safe-c/libc/check.sh`).

[The 47 functions that are errors, and what to use instead.](safe-c-docs/library.md)

Where C has no safe way to do something, small headers in [safe-c-std](safe-c-std) do it, with no unsafe code inside:

| Header | For | Instead of |
|---|---|---|
| [`result.h`](safe-c-std/result.h) | `RESULT`, `OK`, `ERR`, `TRY` | `int` return codes |
| [`text.h`](safe-c-std/text.h) | `str` and `text`: split, trim, compare, numbers, lines | `strtok`, `strchr`, `atoi`, `getline` |
| [`vec.h`](safe-c-std/vec.h) | a growing array, once per type (`#define VEC_NAME`, `VEC_TYPE`); of owners too (`VEC_OWNED`), such as strings from `strdup` | `realloc` by hand |
| [`map.h`](safe-c-std/map.h) | text to plain data | hash tables by hand |
| [`file.h`](safe-c-std/file.h) | read a whole file; replace one all at once | `fread` loops, half-written files |
| [`args.h`](safe-c-std/args.h) | options in `argv` | indexing `argv` |
| [`json.h`](safe-c-std/json.h) | JSON, kept by index | trees of pointers |
| [`pool.h`](safe-c-std/pool.h) | handles that know when they are stale | pointers between nodes of a graph |

## Get started

1. **Build the compiler** ([building](safe-c-docs/building.md)), then compile one file: `clang -fsafe-c parser.c`. Start with the
   file that reads untrusted input.
2. **Fix what it reports.** Errors say what is wrong and what to write.
3. **The marks you meet first:**

   | Write | When |
   |---|---|
   | `_Nullable` | the pointer may be null |
   | `__counted_by(n)` | the pointer has a length, `n` |
   | `_Owned` | whoever holds it must free it |
   | `_Take(place)` | move an owner out of a field, leaving null; `(void)_Take(p)` lets it go on purpose |
   | `_View` | a struct that borrows, kept on the stack |

4. **Not ready to fix a spot?** `__unsafe { }` around it, or `#pragma clang safe_c legacy` at the top of a header. A plain C
   library is called from a small `__unsafe` function, which answers for what it passes.

Every mark: [annotations](safe-c-docs/annotations.md). Every rule: [rules](safe-c-docs/rules.md) (frozen; ideas wait in
[later](safe-c-docs/later.md)).

## Limits

| | |
|---|---|
| **Not proven sound** | small Lean models cover ownership; fields, views, strings and bounds are argued and tested, not proved, and Clang itself is not proved |
| **Attacked four times** | blind attacks found 34, 32, 33, then 19 root causes, all fixed; the fixes and the later rules have not been attacked |
| **Threads** | data races are checked only for data marked `_Guarded_by` a lock |
| **Containers** | `vec.h` holds owners (`VEC_OWNED`); `map.h` holds plain data |
| **Platform** | macOS (arm64) with Apple's SDK; Linux builds, but glibc's headers have no annotations |
| **Cost** | 0% to 7% run time on the programs above, against the same code in plain Clang; ported libraries from 0.58x to 1.28x |

Ported libraries: jsmn, inih, parson and cJSON with ownership on (cJSON's references are not sound), lz4 without. Every open item:
[open issues](safe-c-notes/review-2026-11-23.md), and `clang/utils/safe-c/holes`.

## Juliet

<!-- generated:summary -->
**None missed.** On a ported copy of NIST's Juliet suite, 6,773 of 11,522 cases in 10 bug classes (those whose fixed version builds after porting):

| Bugs | Caught | When |
|---|---|---|
| Null, double free, use after free | **608 of 608** | compile time, as in Rust |
| Out of bounds, divide by zero, that the harness can make happen | **5,545 of 5,545** | at the bad access |

Not counted: 394 need an input the harness does not give (a random value, a file, a socket), and 226 are not bugs on this target (32-bit-only overflows, floating-point division, buffers that stay terminated). 4,749 cases do not build after porting. For 3,221 of the traps the harness could not rule out one raised by the platform, and the run predates the current ownership checker.

A case counts only when Safe C gives a compile error or traps at the bug. [Full table, the port and the exclusions](safe-c-docs/results.md).
<!-- end generated:summary -->

## More

- [Safe C for Rust programmers](safe-c-docs/for-rust-programmers.md)
- [Examples](safe-c-docs/examples.md), [adoption](safe-c-docs/adoption.md), [results](safe-c-docs/results.md), [building](safe-c-docs/building.md)

Apache License v2.0 with LLVM Exceptions, like LLVM.
