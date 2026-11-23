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

/* Bounds annotations: Apple's ptrcheck.h defines them, and defines them as
   nothing when bounds safety is off. Without ptrcheck.h, do the same here. */
#if defined(__has_include)
#if __has_include(<ptrcheck.h>)
#include <ptrcheck.h>
#define __SAFEC_HAVE_PTRCHECK 1
#endif
#endif

#ifndef __SAFEC_HAVE_PTRCHECK
#define __single
#define __indexable
#define __bidi_indexable
#define __unsafe_indexable
#define __counted_by(N)
#define __counted_by_or_null(N)
#define __sized_by(N)
#define __sized_by_or_null(N)
#define __ended_by(E)
#define __terminated_by(T)
#define __null_terminated
#endif

/* libc string functions. The system headers leave their `const char *`
   parameters unannotated, so bounds safety would hand any buffer to them
   unchecked, and strlen() of an unterminated buffer reads past its end. These
   redeclarations add __null_terminated to the read-only string parameters
   (types are otherwise identical, so they merge with the system declarations
   in either order). A buffer with known bounds passed to one of them is then
   scanned for its terminator, and the program traps if there is none.
   Known gap: printf-style variadic arguments cannot be annotated this way. */
#if defined(__SAFEC_HAVE_PTRCHECK) && __has_ptrcheck
typedef __SIZE_TYPE__ __safec_size_t;
__safec_size_t strlen(const char *__null_terminated);
int strcmp(const char *__null_terminated, const char *__null_terminated);
int strncmp(const char *__null_terminated, const char *__null_terminated,
            __safec_size_t);
char *strchr(const char *__null_terminated, int);
char *strrchr(const char *__null_terminated, int);
char *strstr(const char *__null_terminated, const char *__null_terminated);
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
#if !defined(__clang__)
#define _Nonnull
#define _Nullable
#define _Null_unspecified
#endif

#endif /* __SAFEC_H */
