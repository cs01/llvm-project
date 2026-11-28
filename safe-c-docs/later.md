# Later

Ideas that wait while the language is frozen ([rules.md](rules.md)). Each comes back only when a real program needs it.

| Idea | Why it waits |
|---|---|
| `?:` choosing between two pointers | write `if`; seen once, in the editor |
| One mark for a wrapper around system calls (today `_Trusted __unsafe`) | wording, not safety |
| Owners inside unions, and in `map.h` | `vec.h` holds owners now (`VEC_OWNED`); nothing has needed the rest |
| Lifetimes, as in Rust | none of the programs needed them, and they would stop it reading as C |
| Threads | out of scope |
| Plain C libraries | out of scope: call them from `__unsafe` |
| A result checked where it is set, `if ((n = f()).ok)` | write `n = f(); if (n.ok)` |
| A result kept in a variable that is never read | not a memory error: the value still cannot be read unchecked |
| Implicit narrowing is an error (`long` to `int`, `size_t` to `uint16_t`); a cast says it is meant | minmea's bug #106 was one (`strtol` into an `int`, then `abs(INT_MIN)`): 5 such lines in minmea, 0 in our own code, 21 bounded ones in heatshrink that would need casts |
| Signed overflow traps instead of wrapping, with a per-file switch for arithmetic that means to wrap | would have stopped minmea's two overflows at the line; costs a little speed. For flight code, the one to want |
| Fixed-width types only (`int32_t`, not `int`) | portability, not memory safety: left to MISRA and linters |
| A struct that names the one function that frees it, `_Drop(node_free)` | tried on parson and cJSON (2026-11-27): it caught nothing the rules do not. A struct that holds pointers cannot become `void *` or be freed before its fields, so it is already freed only through a typed function of the program. The mixed-allocator bugs seen in real libraries are on `char *` (`sqlite3_free`, `curl_free`, `xmlFree`, `g_free`), which a mark on a struct cannot say; their headers can, with GCC's `malloc(dealloc, index)`, which the compiler reads |
| `strtol`'s end pointer | it can write to text that must not change; pass `NULL` and use `str_int` |

What is not frozen: proofs of the rules there are, fixes for holes, wrappers, and error messages.
