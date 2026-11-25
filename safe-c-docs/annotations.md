# What you annotate

[Back to the README](../README.md)

## What you annotate

Most pointers need nothing: unannotated means non-null and one element, which is the
safe choice. You annotate only where a pointer does more:

<!-- generated:annotations -->
| Annotation | Meaning | In Rust | Example | From |
|---|---|---|---|---|
| *(nothing)* | non-null, exactly one element | `&T` | `void reset(struct conn *c);` | Safe C default |
| `_Nullable` | may be null; must be checked before use (with `__counted_by(n)`: null, or `n` elements) | `Option<&T>`: plain `&T` is never null | `char *_Nullable getenv(const char *name);` | Clang |
| `__counted_by(n)` | points to `n` elements; `n` is another parameter or field | `&[T]` (a slice: pointer plus length) | `int sum(const int *__counted_by(n) v, size_t n);` | Apple |
| `__sized_by(n)` | points to `n` bytes | `&[u8]` | `void fill(void *__sized_by(n) dst, size_t n);` | Apple |
| `__ended_by(end)` | valid up to the pointer `end` | a slice given as two pointers (`slice::from_ptr_range`) | `int scan(const char *__ended_by(end) p, const char *end);` | Apple |
| `__null_terminated` | a string ending in 0 (string literals and libc strings are already) | `&CStr` | `size_t my_strlen(const char *__null_terminated s);` | Apple |
| `_Owned` | on a parameter: the function takes ownership (frees it, or keeps it to free later); the caller may not use it afterwards | taking a `Box<T>` by value (a move) | `void json_delete(struct json *_Owned item);` | Safe C |
| `T *_Owned` (field, local, result) | this pointer is responsible for freeing its target; it is moved, never copied | `Box<T>` | `struct node { int v; struct node *_Owned _Nullable next; };` | Safe C |
| `_Take(place)` | moves an owned pointer out of a field or global, leaving null | `Option::take` | `struct node *_Owned _Nullable pop(struct node *n) { return _Take(n->next); }` | Safe C |
| `T *_Static` | points only at string literals, globals or functions | `&'static T` | `struct op { const char *_Static name; int code; };` | Safe C |
| `_Noescape` | on a parameter: the function does not keep the pointer | a plain `&T` parameter, which cannot outlive the call | `size_t count_spaces(const char *s _Noescape);` | Safe C (Clang's `noescape`) |
| `_Tagged_by(f)`, `_When(V)` | a union whose live member is named by the sibling field `f`: a member is read or written only where `f` is known to be one of its `_When` values; a `switch` on `f` must handle every value | an `enum` with data, and `match` | `struct shape { int kind; union _Tagged_by(kind) { double radius _When(0); double side _When(1); }; };` | Safe C |
| `_Guarded_by(m)` | data read or written only while holding lock `m`; Clang's thread-safety analysis checks it | `Mutex<T>` | `struct _Capability("mutex") mtx { int state; }; struct mtx stats_lock; int hits _Guarded_by(stats_lock);` | Safe C (Clang's `guarded_by`) |
| `_Requires(m)`, `_Acquires(m)`, `_Releases(m)` | a function's callers must hold `m`; or it takes or releases `m` | a `MutexGuard` held across a call | `void bump(void) _Requires(&stats_lock);` | Safe C (Clang's thread-safety attributes) |
| `_Taken_if_true`, `_Taken_if_zero` | on a parameter: the function takes ownership only when it returns true (or zero) | returning the `Box` back in `Err` | `_Bool append(struct node *list, struct node *_Taken_if_true n);` | Safe C |
| `_Trusted` | a function whose ownership is not checked inside; its callers are held to its signature | a safe function around an `unsafe` body | `_Trusted void swap(struct node *a, struct node *b);` | Safe C |
| `__unsafe` | a block or function where Safe C's rules are lifted, for code that cannot be checked | `unsafe { }` | `int first(va_list ap) { int v; __unsafe { v = va_arg(ap, int); } return v; }` | Safe C |
| `#pragma clang safe_c legacy` | in a header not yet ported: its declarations are trusted, its code unchecked | an `extern "C"` block over unaudited C | first line of a header not yet ported | Safe C |

The Rust column is the closest equivalent, not an exact one. A plain pointer may write the
data it points to, but cannot free or replace what that data owns; that takes `_Exclusive`
(Rust's `&mut T`), and lending one ends the caller's other borrows of it. Locals need no
kind: one that only ever holds owned values is an owner, as `let x = Box::new(..)` is.
Rust's rule that two plain pointers may not both write the same data at once is not
enforced (it matters for threads, M5).
<!-- end generated:annotations -->

All of these come from `safec.h`, which `-fsafe-c` includes automatically, so
Safe C code does not need to include it.
