/*===---- safec.h - Safe C annotations -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 *
 * One header for every Safe C annotation. With the Safe C compiler the
 * annotations are checked; with any other C compiler (GCC, MSVC, stock Clang)
 * they expand to nothing and the code compiles as ordinary C. The file has no
 * dependencies, so a project can copy it into its own tree.
 *
 * -fsafe-c includes this header automatically.
 */

#ifndef __SAFEC_H
#define __SAFEC_H

/* What you annotate. One line per annotation; the README's table is generated
   from these lines (clang/utils/safe-c/gen-readme.ts), so keep them current.
   @annotation `_Nullable` | may be null; must be checked before use | `Option<&T>`: plain `&T` is never null
   @annotation `__counted_by(n)` | points to `n` elements; `n` is another parameter or field | `&[T]` (a slice: pointer plus length)
   @annotation `__counted_by_or_null(n)` | as `__counted_by(n)`, or null | `Option<&[T]>`
   @annotation `__sized_by(n)` | points to `n` bytes | `&[u8]`
   @annotation `__ended_by(end)` | valid up to the pointer `end` | a slice given as two pointers (`slice::from_ptr_range`)
   @annotation `__null_terminated` | a string ending in 0 (string literals and libc strings are already) | `&CStr`
   @annotation `__safec_takes(i)` | the function frees (or keeps) its `i`th parameter; callers may not use it afterwards | taking a `Box<T>` by value (a move)
   @annotation `__safec_returns_owned` | the result is new memory the caller must free | returning `Box<T>`
   @annotation `__safec_lifetimebound` | on a parameter: the result points into it and dies with it | `fn f<'a>(x: &'a T) -> &'a U`
   @annotation `__safec_noescape` | on a parameter: the function does not keep the pointer | a plain `&T` parameter, which cannot outlive the call
   @annotation `__unsafe` | a block or function where Safe C's rules are lifted, for code that cannot be checked | `unsafe { }`
   @annotation `#pragma clang safe_c legacy` | in a header not yet ported: its declarations are trusted, its code unchecked | an `extern "C"` block over unaudited C
   Everything else needs nothing: an unannotated pointer is non-null and points to
   exactly one element, like Rust's `&T`. */

/* Bounds annotations: Apple's ptrcheck.h defines them, and defines them as
   nothing when bounds safety is off. Without ptrcheck.h, do the same here. */
#if defined(__has_include)
#if __has_include(<ptrcheck.h>)
#include <ptrcheck.h>
#define __SAFEC_HAVE_PTRCHECK 1
#endif
#endif

/* Some ptrcheck.h versions (Apple clang 21) define only a few macros when bounds
   safety is off, so fill in each missing one. */
#ifndef __single
#define __single
#endif
#ifndef __indexable
#define __indexable
#endif
#ifndef __bidi_indexable
#define __bidi_indexable
#endif
#ifndef __unsafe_indexable
#define __unsafe_indexable
#endif
#ifndef __counted_by
#define __counted_by(N)
#endif
#ifndef __counted_by_or_null
#define __counted_by_or_null(N)
#endif
#ifndef __sized_by
#define __sized_by(N)
#endif
#ifndef __sized_by_or_null
#define __sized_by_or_null(N)
#endif
#ifndef __ended_by
#define __ended_by(E)
#endif
#ifndef __terminated_by
#define __terminated_by(T)
#endif
#ifndef __null_terminated
#define __null_terminated
#endif
#ifndef __unsafe_forge_single
#define __unsafe_forge_single(T, P) ((T)(P))
#endif
#ifndef __unsafe_forge_bidi_indexable
#define __unsafe_forge_bidi_indexable(T, P, S) ((T)(P))
#endif
#ifndef __unsafe_forge_null_terminated
#define __unsafe_forge_null_terminated(T, P) ((T)(P))
#endif
#ifndef __null_terminated_to_indexable
#define __null_terminated_to_indexable(P) (P)
#endif
#ifndef __unsafe_null_terminated_to_indexable
#define __unsafe_null_terminated_to_indexable(P) (P)
#endif
#ifndef __unsafe_null_terminated_from_indexable
#define __unsafe_null_terminated_from_indexable(P, ...) (P)
#endif

/* libc string functions. The system headers leave their `const char *`
   parameters unannotated, so bounds safety would hand any buffer to them
   unchecked, and strlen() of an unterminated buffer reads past its end. These
   redeclarations add __null_terminated to the read-only string parameters
   (types are otherwise identical, so they merge with the system declarations
   in either order). A buffer with known bounds passed to one of them is then
   scanned for its terminator, and the program traps if there is none.
   Known gap: printf-style variadic arguments cannot be annotated this way. */
/* Apple's libc annotates these itself when __LIBC_STAGED_BOUNDS_SAFETY_ATTRIBUTES
   is set (the driver sets it under -fsafe-c), and its declarations would
   conflict with these. Where it leaves a string parameter __unsafe_indexable
   (strncmp), Safe C accepts terminated strings (checkSafeCUncheckedParams). */
#if defined(__SAFEC_HAVE_PTRCHECK) && __has_ptrcheck &&                        \
    !defined(__LIBC_STAGED_BOUNDS_SAFETY_ATTRIBUTES)
typedef __SIZE_TYPE__ __safec_size_t;
/* The read-only scans are also marked pure, so Safe C code marked
   __attribute__((pure)) may call them. */
__attribute__((__pure__)) __safec_size_t strlen(const char *__null_terminated);
__attribute__((__pure__)) int strcmp(const char *__null_terminated,
                                     const char *__null_terminated);
__attribute__((__pure__)) int strncmp(const char *__null_terminated,
                                      const char *__null_terminated,
                                      __safec_size_t);
__attribute__((__pure__)) char *strchr(const char *__null_terminated, int);
__attribute__((__pure__)) char *strrchr(const char *__null_terminated, int);
__attribute__((__pure__)) char *strstr(const char *__null_terminated,
                                       const char *__null_terminated);
char *strcpy(char *, const char *__null_terminated);
char *strcat(char *, const char *__null_terminated);
char *strdup(const char *__null_terminated);
int puts(const char *__null_terminated);
int atoi(const char *__null_terminated);
long atol(const char *__null_terminated);
double atof(const char *__null_terminated);
char *getenv(const char *__null_terminated);
#endif

/* __unsafe marks a block or a function in which Safe C's own rules are not
   enforced: `__unsafe { ... }` or `__unsafe int f(void) { ... }`. Elsewhere
   it expands to nothing, and `__unsafe { ... }` is an ordinary block. */
#if defined(__has_c_attribute)
#if __has_c_attribute(clang::suppress)
#define __unsafe [[clang::suppress("safe-c")]]
#endif
#endif
#ifndef __unsafe
#define __unsafe
#endif

/* Nullability qualifiers are Clang keywords; other compilers do not have them. */
/* Ownership at interfaces, read by Clang's lifetime analysis. __safec_takes(N):
   the function frees (or keeps) its Nth parameter, so the caller must not use
   it afterwards. __safec_returns_owned: the result is new memory the caller
   must free. __safec_lifetimebound on a parameter: the result points into it.
   __safec_noescape on a parameter: the function does not keep the pointer
   (lets a buffer be passed where a terminated string is expected).
   Safe C checks each against the function body. */
#if defined(__clang__)
#define __safec_takes(N) __attribute__((ownership_takes(malloc, N)))
#define __safec_returns_owned __attribute__((ownership_returns(malloc)))
#define __safec_lifetimebound __attribute__((lifetimebound))
#define __safec_noescape __attribute__((noescape))
#else
#define __safec_takes(N)
#define __safec_returns_owned
#define __safec_lifetimebound
#define __safec_noescape
#endif

#if !defined(__clang__)
#define _Nonnull
#define _Nullable
#define _Null_unspecified
#endif

#endif /* __SAFEC_H */
