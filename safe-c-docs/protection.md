# How Safe C closes each problem

[Back to the README](../README.md)

## What makes C unsafe

- **Out-of-bounds reads and writes**
- **Null dereference**
- **Use after free and double free**
- **Uninitialized reads**
- **Type confusion**: bad pointer casts, union punning, mismatched `printf` arguments
- **Integer undefined behavior**: signed overflow, oversized shifts, division by zero
- **Data races**
- **Escape hatches**: flags, pragmas, attributes and builtins that switch checks off

## How Safe C closes each one

| Problem | How Safe C stops it | Built on | Status |
|---|---|---|---|
| Out of bounds | `__counted_by`, `__sized_by`, `__ended_by` on interfaces; locals carry their own bounds; every access proven at compile time or trapped | Apple's `-fbounds-safety` | works |
| Null dereference | non-null by default; `_Nullable` must be checked before use, tracked through each function | this fork | works |
| Use after free, double free | Clang's lifetime analysis always on, as errors, extended with Rust's ownership rules: every pointer in a field or global says whether it owns, borrows only static data, or is unchecked; owned pointers move and are freed once; borrows end when their owner changes; interfaces say what they take, return and borrow from, checked against bodies | Clang's lifetime safety (usx95), this fork | works: owned fields and globals (`_Owned`, `_Take`), exclusive borrows (`_Exclusive`), views, struct copies; tested on jsmn and cJSON |
| Uninitialized reads | a maybe-uninitialized local is an error; stack aggregates and `malloc` memory start zeroed | Clang's uninitialized analysis, `-ftrivial-auto-var-init` | works |
| Type confusion | pointer casts only to `void *`, to the same type, or between types without pointers; no cast removes `const`; bytes never become pointers or `_Bool` (`memcpy`, `read`, `arc4random_buf` included); unions likewise; no integer-to-pointer; `printf`/`scanf` checked, `%s` buffers checked for a terminator; `va_arg` only in `__unsafe`; a symbol declared with another type in another file does not link | this fork, rule from the Lean model | works |
| Unchecked libc | `strcpy`, `sprintf`, `gets` unavailable; a bounded pointer cannot go to an unchecked parameter; `FILE`/`DIR` handles are owned like memory; `setjmp`/`longjmp` only in `__unsafe` | Apple's libc annotations, this fork | works |
| Integer UB | signed overflow wraps (as in Rust release builds); division by zero and oversized shifts trap | `-fwrapv`, Clang's trapping sanitizers | works |
| Data races | a thread's argument is moved into it (an owner, static data or null), and the spawning code may not use it after; data marked `_Guarded_by(lock)` is reached only while holding the lock, functions say `_Requires(lock)`, and locks are released on every path (Clang's thread-safety analysis, always on, as errors) | `-Wthread-safety`, this fork | in progress: mutable globals must next be guarded, atomic or thread-local, and a counted pointer and its count guarded together (a race on the pair breaks bounds checks today) |
| Weakening | weakening flags rejected; safety errors cannot be lowered by `-Wno-` or pragmas; a file cannot declare itself a system header; lying `pure`/`const`/`alloc_size` rejected | this fork | works, except that `-isystem` marks any directory as trusted |
| Escape hatch | `__unsafe { }` blocks and functions; `#pragma clang safe_c legacy` for headers not yet ported; each is one searchable line, and `-Rsafe-c-unsafe` lists them all | this fork | works |

**Why it can be sound.** [safe-c-lean/](../safe-c-lean/) is a Lean 4 model of Safe C's
proposed rules with machine-checked proofs: a program that follows them, outside
`__unsafe`, never reaches undefined behavior (it finishes, traps or runs forever), and the
ownership rules rule out use after free and double free. Every rule has a counterexample
showing the proof fails without it. The cast rule above comes straight from the model.
Clang implements those rules, ownership included: the 18 rules in
[OWNERSHIP.md](../safe-c-lean/OWNERSHIP.md) are each a compiler check with its own test.
