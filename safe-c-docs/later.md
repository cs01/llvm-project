# Later

Ideas that wait while the language is frozen ([rules.md](rules.md)). Each comes back only when a real program needs it.

| Idea | Why it waits |
|---|---|
| `?:` choosing between two pointers | write `if`; seen once, in the editor |
| One mark for a wrapper around system calls (today `_Trusted __unsafe`) | wording, not safety |
| Owners inside containers and unions (a vector of strings) | indexes into one array covered every program so far |
| Lifetimes, as in Rust | none of the programs needed them, and they would stop it reading as C |
| Threads | out of scope |
| Plain C libraries | out of scope: call them from `__unsafe` |
| A result checked where it is set, `if ((n = f()).ok)` | write `n = f(); if (n.ok)` |
| `strtol`'s end pointer | it can write to text that must not change; pass `NULL` and use `str_int` |

What is not frozen: proofs of the rules there are, fixes for holes, wrappers, and error messages.
