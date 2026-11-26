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
   @annotation `_Nullable` | Clang | may be null; must be checked before use (with `__counted_by(n)`: null, or `n` elements) | `Option<&T>`: plain `&T` is never null | `char *_Nullable getenv(const char *name);`
   @annotation `__counted_by(n)` | Apple | points to `n` elements; `n` is another parameter or field | `&[T]` (a slice: pointer plus length) | `int sum(const int *__counted_by(n) v, size_t n);`
   @annotation `__sized_by(n)` | Apple | points to `n` bytes | `&[u8]` | `void fill(void *__sized_by(n) dst, size_t n);`
   @annotation `__ended_by(end)` | Apple | valid up to the pointer `end` | a slice given as two pointers (`slice::from_ptr_range`) | `int scan(const char *__ended_by(end) p, const char *end);`
   @annotation `__null_terminated` | Apple | a string ending in 0 (string literals and libc strings are already) | `&CStr` | `size_t my_strlen(const char *__null_terminated s);`
   @annotation `_Owned` | Safe C | on a parameter: the function takes ownership (frees it, or keeps it to free later); the caller may not use it afterwards | taking a `Box<T>` by value (a move) | `void json_delete(struct json *_Owned item);`
   @annotation `T *_Owned` (field, local, result) | Safe C | this pointer is responsible for freeing its target; it is moved, never copied | `Box<T>` | `struct node { int v; struct node *_Owned _Nullable next; };`
   @annotation `_Take(place)` | Safe C | moves an owned pointer out of a field or global, leaving null | `Option::take` | `struct node *_Owned _Nullable pop(struct node *n) { return _Take(n->next); }`
   @annotation `T *_Static` | Safe C | points only at string literals, globals or functions | `&'static T` | `struct op { const char *_Static name; int code; };`
   @annotation `_Noescape` | Safe C (Clang's `noescape`) | on a parameter: the function does not keep the pointer | a plain `&T` parameter, which cannot outlive the call | `size_t count_spaces(const char *s _Noescape);`
   @annotation `T *_Unchecked` | Safe C | kept for its value: stored, copied and compared freely, followed only inside a `_Trusted` function | `*const T` (a raw pointer) | `struct child { struct child *_Unchecked _Nullable up; };`
   @annotation `struct _View` | Safe C | a struct holding borrows: lives in locals and parameters, and ends with what it borrows | a struct with a lifetime, `struct S<'a>` | `struct _View span { const int *__counted_by(n) p; int n; };`
   @annotation `_Tagged_by(f)`, `_When(V)` | Safe C | a union whose live member is named by the sibling field `f`: a member is read or written only where `f` is known to be one of its `_When` values; a `switch` on `f` must handle every value | an `enum` with data, and `match` | `struct shape { int kind; union _Tagged_by(kind) { double radius _When(0); double side _When(1); }; };`
   @annotation `_Guarded_by(m)` | Safe C (Clang's `guarded_by`) | data read or written only while holding lock `m`; Clang's thread-safety analysis checks it | `Mutex<T>` | `struct _Capability("mutex") mtx { int state; }; struct mtx stats_lock; int hits _Guarded_by(stats_lock);`
   @annotation `_Requires(m)`, `_Acquires(m)`, `_Releases(m)` | Safe C (Clang's thread-safety attributes) | a function's callers must hold `m`; or it takes or releases `m` | a `MutexGuard` held across a call | `void bump(void) _Requires(&stats_lock);`
   @annotation `_Trusted` | Safe C | a function whose ownership is not checked inside; its callers are held to its signature | a safe function around an `unsafe` body | `_Trusted void swap(struct node *a, struct node *b);`
   @annotation `__unsafe` | Safe C | a block or function where Safe C's rules are lifted, except ownership (that is `_Trusted`) | `unsafe { }` | `int first(va_list ap) { int v; __unsafe { v = va_arg(ap, int); } return v; }`
   @annotation `#pragma clang safe_c legacy` | Safe C | in a header not yet ported: its declarations are trusted, its code unchecked | an `extern "C"` block over unaudited C | first line of a header not yet ported
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

/* _No_bounds_checks on an `__unsafe` function: compiled without run-time
   bounds, null and integer checks, for an audited hot loop whose own tests already keep every
   access in bounds (say which, in a comment). Use as a last resort for speed;
   the compiler should remove such checks itself. */
#if defined(__SAFE_C__)
/* Also the trap checks -fsafe-c adds for null dereference and integer UB:
   the same audited promise covers them. */
#define _No_bounds_checks                                                      \
  __attribute__((annotate("safec_no_bounds_checks"),                          \
                 no_sanitize("null", "signed-integer-overflow",              \
                             "shift-exponent", "integer-divide-by-zero",     \
                             "vla-bound")))
#else
#define _No_bounds_checks
#endif

/* Nullability qualifiers are Clang keywords; other compilers do not have them. */
/* On a parameter: the function does not keep the pointer (lets a buffer be
   passed where a terminated string is expected). */
#if defined(__clang__)
#define _Noescape __attribute__((noescape))
/* Pointer kinds go right after the '*' they describe, so a pointer to
   owned pointers is 'char *_Owned *_Owned items'.
   On a parameter, owned means the function takes ownership. */
#if defined(__SAFE_C__)
#define _Owned [[clang::annotate_type("safec_owned")]]
#else /* plain clang: [[]] after _Nullable would not parse */
#define _Owned
#endif
/* On a field or result: points only at static data (string literals,
   globals, functions), like Rust's &'static. */
#if defined(__SAFE_C__)
#define _Static [[clang::annotate_type("safec_static")]]
#else /* plain clang: [[]] after _Nullable would not parse */
#define _Static
#endif
/* On a function: its ownership is not checked inside; its callers are held to
   its signature all the same. */
#define _Trusted __attribute__((annotate("safec_trusted")))
/* A pointer kept for its value: stored, copied and compared freely, followed
   only inside a _Trusted function. */
#if defined(__SAFE_C__)
#define _Unchecked [[clang::annotate_type("safec_unchecked")]]
#else /* plain clang: [[]] after _Nullable would not parse */
#define _Unchecked
#endif
/* On a struct: holds borrows, so it lives only in locals and parameters and
   ends with what it borrows. Behind a pointer it may be changed only if it has
   one pointer, which stays inside what it pointed into. */
#if defined(__clang__)
#define _View __attribute__((annotate("safec_view")))
#else
#define _View
#endif
/* Move an owned pointer out of a field or global, leaving null (and a zero
   count): free(_Take(n->next)). Like Rust's Option::take. */
#if defined(__SAFE_C__)
#define _Take(lv) __builtin_safec_take(lv)
#else /* clang without Safe C has no builtin; lv is evaluated twice */
#define _Take(lv) ({ __typeof__(lv) __safec_t = (lv); (lv) = 0; __safec_t; })
#endif
#else
#define _Noescape
#define _Owned
#define _Static
#define _Unchecked
#define _View
#define _Trusted
/* Evaluates lv twice; fine for the plain lvalues take is used on. */
#define _Take(lv) ({ __typeof__(lv) __safec_t = (lv); (lv) = 0; __safec_t; })
#endif

#if !defined(__clang__)
#define _Nonnull
#define _Nullable
#define _Null_unspecified
#endif


/* Set a pointer and its count together: _Set(s->p, s->n, new_p, new_n). Both
   new values are read, and checked against each other, before either is set. */
#define _Set(ptr, count, new_ptr, new_count) \
  ((void)((ptr) = (new_ptr), (count) = (new_count)))

/* Tagged unions, like Rust's enums with data:
     struct shape {
       enum kind kind;
       union _Tagged_by(kind) {
         struct circle c _When(CIRCLE);
         struct rect r _When(RECT, SQUARE);
       };
     };
   _Tagged_by names the sibling field holding the tag; _When lists the tag
   values for which a member is live. A switch on the tag must handle every
   value (or have a default). */
#if defined(__clang__)
#define _Tagged_by(f) __attribute__((annotate("safec_tagged_by", #f)))
#define _When(...) __attribute__((annotate("safec_when", __VA_ARGS__)))
#else
#define _Tagged_by(f)
#define _When(...)
#endif

/* Data shared between threads, checked by Clang's thread-safety analysis
   (always on under Safe C):
     safec_mutex lock;
     int hits _Guarded_by(lock);          every access must hold lock
     void bump(void) _Requires(lock);     callers must hold lock
   Lock types are declared with _Capability("mutex"); their lock and unlock
   functions with _Acquires(m) and _Releases(m). */
#if defined(__clang__)
#define _Capability(name) __attribute__((capability(name)))
#define _Guarded_by(m) __attribute__((guarded_by(m)))
#define _Pt_guarded_by(m) __attribute__((pt_guarded_by(m)))
#define _Requires(...) __attribute__((requires_capability(__VA_ARGS__)))
#define _Acquires(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define _Releases(...) __attribute__((release_capability(__VA_ARGS__)))
#else
#define _Capability(name)
#define _Guarded_by(m)
#define _Pt_guarded_by(m)
#define _Requires(...)
#define _Acquires(...)
#define _Releases(...)
#endif

#endif /* __SAFEC_H */
