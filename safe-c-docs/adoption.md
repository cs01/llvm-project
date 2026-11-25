# Adopting Safe C

> Being rewritten. `_Exclusive`, `_View`, `_Unchecked`, `_Takes`, `_Returns_owned`, `_Lifetimebound`, `_Drop` and
> `_Unsynchronized` no longer exist; [annotations](annotations.md) is current.

[Back to the README](../README.md)

## Same code, any compiler

Safe C does not lock you in. Every annotation comes from one header, `safec.h`:

- **With the Safe C compiler** and `-fsafe-c`, the annotations are checked and the
  safety rules are enforced.
- **With GCC or any Clang** (Xcode's, a stock one), `safec.h` defines every annotation
  as nothing, or as the plain GNU C it stands for. The same source compiles as ordinary
  C, without the checks. Two need GNU C: `_Take` is a statement expression (on MSVC it does
  not build) and `_Drop` is the `cleanup` attribute (on MSVC it expands to nothing, so
  the cleanup does not run).

`safec.h` is a single file with no dependencies. The Safe C compiler ships it; for other
compilers, copy [`clang/lib/Headers/safec.h`](../clang/lib/Headers/safec.h) into your project
(Apache 2.0 with LLVM exceptions, like the rest of LLVM). `-fsafe-c`
includes it automatically.

## Adopting Safe C, one file at a time

Nobody annotates a whole codebase at once. Safe C is adopted per file, and the code
builds and ships at every step.

- **The unit is a file.** `-fsafe-c` is a per-file flag. Safe files and plain C files link
  together unchanged: same ABI.
- **Four levels per file:**

  | Level | What it means | What you get |
  |---|---|---|
  | 0. Plain C | untouched | builds as before |
  | 1. Annotated interface | the header has `_Nullable`, `__counted_by`, ... | every safe caller is checked against it |
  | 2. Safe, with `__unsafe` | `-fsafe-c`, the hard spots wrapped in `__unsafe` | everything outside those blocks is checked |
  | 3. Fully safe | `-fsafe-c`, no `__unsafe` | the full guarantee |

- **Calling code that is not safe yet.** Put `#pragma clang safe_c legacy` at the top of
  a header that is not ready. Safe C then treats it like a system header: its code is not
  checked and its declarations are trusted. The trust boundary is one searchable line per
  header, not one `__unsafe` per call, and `-Rsafe-c-unsafe` lists every legacy header in
  a build. The pragma is an error in the file being compiled (that file is always
  checked), and outside Safe C it does nothing. `#pragma clang system_header` in a user
  file is rejected, so this is the only way to declare one. Not yet done: requiring
  legacy declarations' pointers to be checked before use (today they are trusted as
  declared).
- **Order:** code that handles untrusted input first (parsers, decoders, protocol
  handlers), where most memory-safety CVEs are; leaf utilities next; the core last.
- **Tooling** (planned): an annotator that proposes annotations from inference and
  compiler fix-its, each one checked by the compiler; a report of each file's level and
  `__unsafe` count; and a CI ratchet, so a file that reaches level 2 never goes back.

## Why a fork and not a set of flags

Even where Clang has a feature, a set of flags does not give a guarantee:

- **Pieces are missing.** Bounds-carrying pointers are only in Apple's fork. Flow-sensitive
  null checking, ownership and `__unsafe` are only in this one.
- **Most of it is warnings.** Any of them can be turned off with `-Wno-...` or a pragma.
  Under `-fsafe-c` they are errors that no flag or pragma can lower.
- **Flags can be dropped.** One missing flag in one build file silently removes a check.
  `-fsafe-c` rejects the flags that would weaken it (`-fno-bounds-safety` and so on).
- **The defaults are wrong.** Upstream, an unannotated pointer means "unknown".
  Under `-fsafe-c` it means non-null and exactly one element, so forgetting an
  annotation is a compile error, not a hole. The exception is code you choose to trust:
  system headers, `-isystem` directories and headers marked legacy.
- **The escape hatch must be visible.** The only way out is `__unsafe { }` in the
  source, where a reviewer can see and search for it.

## Standards and portability

The C standard committee is unlikely to adopt any of this soon, and it does not need to.
Compiler extensions are how C has always moved: GNU C, `__attribute__`, Apple's blocks,
Microsoft's SAL. Safe C code stays portable through `safec.h` (see [Same code, any
compiler](#same-code-any-compiler)).

The code is still C. It compiles with the normal C ABI and links with any C library.
