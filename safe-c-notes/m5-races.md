# M5: data races (design, 2026-11-23)

Goal: Rust's `Send` and `Sync` for C, so safe code cannot race. A race is not
only wrong answers: a pointer and its count read in the middle of another
thread's update break the bounds check (audit probes r18, s18, u18).

Rust's model, in two sentences: data crosses to another thread only if it is
`Send` (moved, or shared immutably); data is shared between threads only if
it is `Sync` (immutable, atomic, or behind a lock). Safe C gets the same two
rules from what it already has (M2 ownership) plus Clang's thread-safety
analysis (`-Wthread-safety`), which already works in C.

## Rules

1. **Spawning moves.** `pthread_create(&t, attr, fn, arg)`: `arg` must be an
   owner (moved into the thread, like `free`'s argument: the caller's borrows
   end) or `_Static`, or null. A plain borrow is rejected: the spawning
   function could return, or free, while the thread still uses it (Rust's
   `'static` bound on `spawn`). Owned data reachable from `arg` is safe to hand
   over: M2 already guarantees fields hold no borrows.
2. **Shared mutable state is guarded.** A global (or static local) that is not
   `const` must be one of:
   - `_Atomic` (integers and pointers: C11 atomics),
   - `_Thread_local` (one per thread),
   - guarded by a lock: `int hits _Guarded_by(stats_lock);` (Clang's
     `guarded_by`), and every access must hold that lock (Clang's
     thread-safety analysis, made an error).
   This is Rust's rule that a `static` must be `Sync`, and `static mut` needs
   `unsafe`.
3. **Locks are typed.** safec.h's `safec_mutex` wraps `pthread_mutex_t` as a
   Clang capability, with `mutex_lock`/`mutex_unlock` annotated acquire and
   release, so the analysis knows what is held. `_Requires(lock)` on a
   function says its caller must hold it.
4. **A counted pointer and its count are guarded together.** A
   `__counted_by(n)` field and `n` must share one `_Guarded_by` (or both be
   unshared): a reader must never see one updated and not the other.
5. **Owned data shared across threads needs a lock or a reference count.**
   There is no safe way to give two threads the same `_Owned` pointer except
   through a guarded global or a future `arc` type in safe-c-std.

## Not covered (stated, like Rust)

- Deadlocks and lock ordering (Rust does not prevent them either).
- Atomics with relaxed ordering used to publish non-atomic data: allowed by
  C11 and Rust; correctness, not memory safety, as long as the data itself is
  guarded or atomic.
- Signal handlers: treated like threads (they may run at any point), a
  follow-up.

## Cost to existing code

Rule 2 is the expensive one: every mutable global in a single-threaded
program now needs `_Thread_local`, `_Atomic` or a lock. Rust pays the same
(`static mut` is `unsafe`). For a single-threaded program the cheapest
answer is `_Thread_local`, which is free in practice. The census will measure
how many globals this touches before the rule is turned on.

## Order

1. Rule 1 (spawn moves): one Sema check plus the M2 move machinery.
2. Rule 3 (typed locks) in safec.h / safe-c-std, and thread-safety
   diagnostics made errors under Safe C.
3. Rule 2 (globals), measured on the census first.
4. Rule 4 (counted pointer + count under one guard).
5. The audit race probes (r18, s18, u18) rejected with annotations, not just
   because a field is unannotated.
