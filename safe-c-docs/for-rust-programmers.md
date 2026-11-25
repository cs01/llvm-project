# Safe C for Rust programmers

[Back to the README](../README.md)

| Rust | Safe C |
|---|---|
| `&T` (never null) | `T *` |
| `Option<&T>` | `T *_Nullable`, checked before use |
| `&[T]` | `T *__counted_by(n)` |
| `Box<T>` | `T *_Owned` |
| `Option<Box<T>>`, `Option::take` | `T *_Owned _Nullable`, `_Take(place)` |
| `&mut T` / `&T` | `T *` / `const T *`, for what may be freed or replaced (see below) |
| `&'static T` | `T *_Static` |
| `struct S<'a>` | `struct _View S`: locals and parameters only |
| `*const T` | `T *_Unchecked`, followed only in a `_Trusted` function |
| a safe function with an `unsafe` body | `_Trusted` |
| `unsafe { }` | `__unsafe { }` (does not lift ownership: that is `_Trusted`) |
| `enum` with data, `match` | `union _Tagged_by(kind)`, plain data only |
| `Mutex<T>` | `_Guarded_by(lock)` |

## What is different

- **No lifetimes.** A borrow lives in a local or a parameter. A returned borrow comes from the first borrowed
  parameter; any other function has to be written another way. Rust needs `'a` for what Safe C rejects.
- **Aliasing is allowed.** Several mutable pointers to one object are fine. What ends them is a change to what the
  object owns: freeing, taking, replacing, or a call that may. So data races are not prevented.
- **A function that can fail** takes an owner and frees it, or hands it back as its `_Owned` result.
- **Leaks are allowed**, and there is no `Drop`: free it by name.
- **No generics, traits or shared ownership** (`Rc`). A graph uses handles ([pool.h](../safe-c-std/pool.h)), which
  know when they are stale, or `_Unchecked` pointers and `_Trusted` functions.
- **Not proved.** Rust's checker has years of use. This one has a Lean model of its rules, and a compiler that is not proved to
  follow it.
