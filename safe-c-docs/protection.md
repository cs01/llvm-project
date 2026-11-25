# How Safe C closes each problem

[Back to the README](../README.md)

| Problem | How | Built on |
|---|---|---|
| Out of bounds | counts on interfaces; locals carry their bounds; proven at compile time or trapped | Apple's `-fbounds-safety` |
| Null | non-null by default; `_Nullable` is checked before use. A check is remembered across calls, so one that a call undoes traps instead of failing to compile | this fork |
| Use after free, double free | `_Owned` pointers move; every other pointer is a borrow that ends when its owner changes; borrows are not stored; code with no rule is an error | this fork, on ClangIR |
| Uninitialized reads | an error for locals; locals and `malloc` memory start zeroed | Clang |
| Type confusion | casts, unions, `memcpy` and format strings may not make a pointer out of something else; files that disagree about a type do not link | this fork |
| Library calls | a contract for the most used functions; others need a `_Trusted` wrapper if they take pointers | Apple's annotations, this fork |
| Integer overflow | signed overflow wraps; division by zero and oversized shifts trap | Clang |
| Data races | only data marked `_Guarded_by` | Clang |
| Turning it off | flags that weaken it are rejected, except `-mllvm -safe-c-ownership=false` for one file | this fork |

**What is proved.** [safe-c-lean](../safe-c-lean/) proves, for a model of the ownership rules with one thread, that a checked
program never uses freed memory or frees twice. `_View`, `_Unchecked`, function pointers and the library contracts are argued from that model, not proved.
**Nothing proves that the compiler follows the model**, and bugs in it have been found that way.

**What is trusted.** `__unsafe`, `_Trusted` bodies, legacy and system headers, `-isystem` directories, the library contracts.
