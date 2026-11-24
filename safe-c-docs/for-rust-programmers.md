# Safe C for Rust programmers

[Back to the README](../README.md)

Safe C is C with Rust's memory-safety rules, checked by a Clang fork
(`-fsafe-c`). No new syntax: the annotations come from one header,
`safec.h`, and vanish on other compilers, so the same file still builds with
GCC.

Everything below is real. The program in
[rust_tour.c](../safe-c-std/examples/rust_tour.c) compiles and runs under
Safe C, Apple clang and ASan, and every error is copied from the compiler.

## The map

| Rust | Safe C |
|---|---|
| `&T` (never null) | `T *` (non-null by default) |
| `Option<&T>` | `T *_Nullable`, checked before use |
| `&[T]`, `&str` | `__counted_by(n)`, `SLICE(T)`, `str` |
| `Box<T>` | `T *_Owned` |
| `&mut T` | `T *_Exclusive` |
| `'a` on a return | `_Lifetimebound` on the parameter |
| `&'static T` | `T *_Static` |
| `Option::take` | `_Take(place)` |
| `Drop` | `_Drop(fn)` on a variable |
| `enum` with data | `union _Tagged_by(kind)` + `_When(V)` |
| exhaustive `match` | `switch` on the tag |
| `Result<T, E>`, `?` | `RESULT(...)`, `TRY(...)` |
| `Vec<T>`, `String` | `VEC(T)`, `string` |
| `Mutex<T>` | `_Guarded_by(lock)` on the data |
| `unsafe { }` | `__unsafe { }` |

## Enums with data, and match

```c
// enum Shape { Circle(f64),
//              Rect { w: f64, h: f64 } }
struct shape {
  enum shape_kind kind;
  union _Tagged_by(kind) {
    double radius _When(CIRCLE);
    struct { double w, h; } rect
        _When(RECT);
  };
};

switch (s->kind) {
case CIRCLE:
  return 3.14159 * s->radius * s->radius;
case RECT:
  return s->rect.w * s->rect.h;
}
```

Forget a variant:

```
error: this switch on 'kind' does not
handle RECT, a tag value of the union it
describes; add 'case RECT:' (or
'default:')
```

Read the wrong one:

```c
return s->radius;  // no check
```
```
error: reading 'radius' needs the tag
'kind' known to be CIRCLE here, or the
union's bytes could be another member's;
check it first (as in
'if (x.kind == CIRCLE)' or
'case CIRCLE:')
```

## Ownership and borrowing

```c
// struct Node { v: i32,
//   next: Option<Box<Node>> }
struct node {
  int v;
  struct node *_Owned _Nullable next;
};

// fn push(&mut self, v: i32)
bool push(struct list *_Exclusive l, int v) {
  struct node *n = calloc(1, sizeof *n);
  if (!n)
    return false;
  n->v = v;
  n->next = _Take(l->head);
  l->head = n;  // moves n
  return true;
}
```

The borrow checker's favorite error, in C:

```c
struct node *first = l->head;
free_list(l);      // takes l as _Exclusive
return first->v;
```
```
error: this pointer is borrowed from
'l->head', and the borrow ends (the
field is taken, replaced, or lent to an
exclusive parameter) before its last
use; read 'l->head' again after that
point, or finish using the pointer
first
note: the borrow ends here
note: later used here
```

Return a reference to a local:

```c
str e5(void) {
  char buf[8] = "hi";
  return (str){buf, 2};
}
```
```
error: stack memory associated with
local variable 'buf' is returned
```

## Result and ?

```c
// fn sum_line(line: &str)
//     -> Result<i64, ParseError>
i64_result sum_line(str line) {
  int64_t total = 0;
  str part = S("");
  while (str_split(&line, ',', &part))
    total += TRY(
        str_to_i64(str_trim(part)),
        i64_result);
  return OK(i64_result, total);
}

i64_result bad = sum_line(S("1, 2x"));
if (!bad.ok && bad.err.kind ==
                 PARSE_BAD_CHAR)
  printf("bad '%c' at %zu\n",
         bad.err.bad.c, bad.err.bad.at);
```

Errors carry data, like a Rust enum variant: `str_to_i64` says which character
was bad and where. Skip the check:

```c
i64_result r = str_to_i64(s);
return r.value;
```
```
error: reading 'value' needs the tag
'ok' known to be true here, or the
union's bytes could be another member's;
check it first (as in 'if (x.ok)')
```

## Bounds, null, Drop

```c
int a[4] = {0};
SLICE(int) v = {a, 8};
```
```
error: initializing 'v.p' ... and count
value of 8 with array 'a' (which has 4
elements) always fails
```

```c
struct node *_Nullable n = find(3);
return n->v;
```
```
error: dereference of nullable pointer
note: add a null check before
dereferencing, or annotate as
'_Nonnull' if this pointer cannot be
null
```

```c
{
  int_vec nums _Drop(int_vec_free) = {0};
  vec_push(&nums, 42);
}  // freed here, on every path out
```

Where an index cannot be proven at compile time, it is checked when it runs
and traps (like a Rust panic). Integer overflow wraps, as in Rust release
builds; division by zero traps.

## What is different from Rust

- **No generics.** `RESULT`, `VEC` and `SLICE` are macros that stamp out one
  type per use: `typedef VEC(int) int_vec;`. It is what Rust's compiler does,
  done by hand.
- **No traits, closures or async.** Function pointers and context structs, as
  in C.
- **Lifetimes are simpler.** One `_Lifetimebound` parameter per returned
  borrow, no named lifetimes. Some things Rust accepts need restructuring or
  `__unsafe`, like a doubly linked list's back pointer (which Rust cannot
  check either).
- **Threads, partly done.** A thread takes ownership of its argument
  (Rust's `Send` on `spawn`), and data marked `_Guarded_by(lock)` is only
  touched with the lock held. Not yet: requiring every mutable global to be
  guarded, atomic or thread-local (Rust's `Sync` on statics).
- **Early.** Two real libraries are ported (jsmn, cJSON), and lz4 is in
  progress. Results and open issues are in [results](results.md).
