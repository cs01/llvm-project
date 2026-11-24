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
| `__counted_by_or_null(n)` | the same as `_Nullable __counted_by(n)` under Safe C; kept for code written for Apple's compiler | `Option<&[T]>` | `struct buf { char *_Nullable __counted_by_or_null(len) data; size_t len; };` | Apple |
| `__sized_by(n)` | points to `n` bytes | `&[u8]` | `void fill(void *__sized_by(n) dst, size_t n);` | Apple |
| `__ended_by(end)` | valid up to the pointer `end` | a slice given as two pointers (`slice::from_ptr_range`) | `int scan(const char *__ended_by(end) p, const char *end);` | Apple |
| `__null_terminated` | a string ending in 0 (string literals and libc strings are already) | `&CStr` | `size_t my_strlen(const char *__null_terminated s);` | Apple |
| `__safec_takes(i)` | old form of `_Owned` on a parameter, by index; the function frees (or keeps) its `i`th parameter; callers may not use it afterwards | taking a `Box<T>` by value (a move) | `__safec_takes(1) void json_free(struct json *item);` (prefer `_Owned` on the parameter) | Safe C (Clang's `ownership_takes`) |
| `_Owned` | on a parameter: the function takes ownership (frees it, or keeps it to free later); the caller may not use it afterwards | taking a `Box<T>` by value (a move) | `void json_delete(struct json *_Owned item);` | Safe C |
| `T *_Owned` (field, global) | this pointer is responsible for freeing its target; it is moved, never copied | `Box<T>` | `struct node { int v; struct node *_Owned _Nullable next; };` | Safe C |
| `_Take(place)` | moves an owned pointer out of a field or global, leaving null | `Option::take` | `struct node *_Nullable pop(struct node *_Excl n) { return _Take(n->next); }` | Safe C |
| `T *_Excl` | an exclusive borrow: the function may take, free or replace owned fields through it | `&mut T` | `void clear(struct node *_Excl n);` | Safe C |
| `T *_Static` | points only at string literals, globals or functions | `&'static T` | `struct op { const char *_Static name; int code; };` | Safe C |
| `T *_Unchecked` | may point anywhere; its value is used only inside `__unsafe` | `*const T` (a raw pointer) | `struct node2 { struct node2 *_Unchecked _Nullable prev; };` | Safe C |
| `struct _View` | a struct holding borrows; lives only in locals and parameters | a struct with a lifetime, `struct S<'a>` | `struct _View span { const int *__counted_by(n) p; int n; };` | Safe C |
| `_Returns_owned` | the result is new memory the caller must free | returning `Box<T>` | `_Returns_owned struct json *json_parse(const char *text);` | Safe C (Clang's `ownership_returns`) |
| `_Lifetimebound` | on a parameter: the result points into it and dies with it | `fn f<'a>(x: &'a T) -> &'a U` | `const char *name_of(const struct json *j _Lifetimebound);` | Safe C (Clang's `lifetimebound`) |
| `_Noescape` | on a parameter: the function does not keep the pointer | a plain `&T` parameter, which cannot outlive the call | `size_t count_spaces(const char *s _Noescape);` | Safe C (Clang's `noescape`) |
| `__unsafe` | a block or function where Safe C's rules are lifted, for code that cannot be checked | `unsafe { }` | `int first(va_list ap) { int v; __unsafe { v = va_arg(ap, int); } return v; }` | Safe C |
| `#pragma clang safe_c legacy` | in a header not yet ported: its declarations are trusted, its code unchecked | an `extern "C"` block over unaudited C | first line of a header not yet ported | Safe C |

The Rust column is the closest equivalent, not an exact one: a plain Safe C pointer can be
written through (like `&mut T`), and Rust's one-writer-at-a-time rule is not enforced yet;
exclusive borrows (`&mut T` proper) come with static ownership (M2).
<!-- end generated:annotations -->

All of these come from `safec.h`, which `-fsafe-c` includes automatically, so
Safe C code does not need to include it.
