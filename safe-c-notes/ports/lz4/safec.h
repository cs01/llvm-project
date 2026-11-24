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
   @annotation `__counted_by_or_null(n)` | Apple | the same as `_Nullable __counted_by(n)` under Safe C; kept for code written for Apple's compiler | `Option<&[T]>` | `struct buf { char *_Owned _Nullable __counted_by_or_null(len) data; size_t len; };`
   @annotation `__sized_by(n)` | Apple | points to `n` bytes | `&[u8]` | `void fill(void *__sized_by(n) dst, size_t n);`
   @annotation `__ended_by(end)` | Apple | valid up to the pointer `end` | a slice given as two pointers (`slice::from_ptr_range`) | `int scan(const char *__ended_by(end) p, const char *end);`
   @annotation `__null_terminated` | Apple | a string ending in 0 (string literals and libc strings are already) | `&CStr` | `size_t my_strlen(const char *__null_terminated s);`
   @annotation `_Takes(i)` | Safe C (Clang's `ownership_takes`) | old form of `_Owned` on a parameter, by index; the function frees (or keeps) its `i`th parameter; callers may not use it afterwards | taking a `Box<T>` by value (a move) | `_Takes(1) void json_free(struct json *item);` (prefer `_Owned` on the parameter)
   @annotation `_Owned` | Safe C | on a parameter: the function takes ownership (frees it, or keeps it to free later); the caller may not use it afterwards | taking a `Box<T>` by value (a move) | `void json_delete(struct json *_Owned item);`
   @annotation `T *_Owned` (field, global) | Safe C | this pointer is responsible for freeing its target; it is moved, never copied | `Box<T>` | `struct node { int v; struct node *_Owned _Nullable next; };`
   @annotation `_Take(place)` | Safe C | moves an owned pointer out of a field or global, leaving null | `Option::take` | `struct node *_Nullable pop(struct node *_Exclusive n) { return _Take(n->next); }`
   @annotation `T *_Exclusive` | Safe C | an exclusive borrow: the function may take, free or replace owned fields through it | `&mut T` | `void clear(struct node *_Exclusive n);`
   @annotation `T *_Static` | Safe C | points only at string literals, globals or functions | `&'static T` | `struct op { const char *_Static name; int code; };`
   @annotation `T *_Unchecked` | Safe C | may point anywhere; its value is used only inside `__unsafe` | `*const T` (a raw pointer) | `struct node2 { struct node2 *_Unchecked _Nullable prev; };`
   @annotation `struct _View` | Safe C | a struct holding borrows; lives only in locals and parameters | a struct with a lifetime, `struct S<'a>` | `struct _View span { const int *__counted_by(n) p; int n; };`
   @annotation `_Returns_owned` | Safe C (Clang's `ownership_returns`) | the result is new memory the caller must free | returning `Box<T>` | `_Returns_owned struct json *json_parse(const char *text);`
   @annotation `_Lifetimebound` | Safe C (Clang's `lifetimebound`) | on a parameter: the result points into it and dies with it; not needed when it is the only pointer parameter (inferred, as Rust elides it) | `fn f<'a>(x: &'a T, y: &U) -> &'a U` | `const char *pick(const struct json *j _Lifetimebound, const char *fallback);`
   @annotation `_Noescape` | Safe C (Clang's `noescape`) | on a parameter: the function does not keep the pointer | a plain `&T` parameter, which cannot outlive the call | `size_t count_spaces(const char *s _Noescape);`
   @annotation `_Tagged_by(f)`, `_When(V)` | Safe C | a union whose live member is named by the sibling field `f`: a member is read or written only where `f` is known to be one of its `_When` values; a `switch` on `f` must handle every value | an `enum` with data, and `match` | `struct shape { int kind; union _Tagged_by(kind) { double radius _When(0); double side _When(1); }; };`
   @annotation `_Drop(fn)` | Safe C (the `cleanup` attribute) | calls `fn(&x)` when `x` goes out of scope, on every path; `x` is never given away by name, only through `_Take` | `Drop` | `struct guard { int held; }; void release(struct guard *g); void use_guard(void) { struct guard g _Drop(release) = {1}; }`
   @annotation `_Guarded_by(m)` | Safe C (Clang's `guarded_by`) | data read or written only while holding lock `m`; Clang's thread-safety analysis checks it | `Mutex<T>` | `struct _Capability("mutex") mtx { int state; }; struct mtx stats_lock; int hits _Guarded_by(stats_lock);`
   @annotation `_Unsynchronized` | Safe C | on a global written only before any thread starts (settings, hooks): it needs no lock, but every write must be inside `__unsafe` | `static mut` | `static int log_level _Unsynchronized;`
   @annotation `_Requires(m)`, `_Acquires(m)`, `_Releases(m)` | Safe C (Clang's thread-safety attributes) | a function's callers must hold `m`; or it takes or releases `m` | a `MutexGuard` held across a call | `void bump(void) _Requires(&stats_lock);`
   @annotation `__unsafe` | Safe C | a block or function where Safe C's rules are lifted, for code that cannot be checked | `unsafe { }` | `int first(va_list ap) { int v; __unsafe { v = va_arg(ap, int); } return v; }`
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
   bounds checks, for an audited hot loop whose own tests already keep every
   access in bounds (say which, in a comment). Use as a last resort for speed;
   the compiler should remove such checks itself. */
#if defined(__SAFE_C__)
#define _No_bounds_checks __attribute__((annotate("safec_no_bounds_checks")))
#else
#define _No_bounds_checks
#endif

/* Nullability qualifiers are Clang keywords; other compilers do not have them. */
/* Ownership at interfaces, read by Clang's lifetime analysis. _Takes(N):
   the function frees (or keeps) its Nth parameter, so the caller must not use
   it afterwards. _Returns_owned: the result is new memory the caller
   must free. _Lifetimebound on a parameter: the result points into it.
   _Noescape on a parameter: the function does not keep the pointer
   (lets a buffer be passed where a terminated string is expected).
   Safe C checks each against the function body. */
#if defined(__clang__)
#define _Takes(N) __attribute__((ownership_takes(malloc, N)))
#define _Returns_owned __attribute__((ownership_returns(malloc)))
#define _Lifetimebound __attribute__((lifetimebound))
#define _Noescape __attribute__((noescape))
/* Pointer kinds go right after the '*' they describe, so a pointer to
   owned pointers is 'char *_Owned *_Owned items'.
   On a parameter, owned means the function takes ownership: Sema turns it
   into ownership_takes at the parameter's index, so no index can drift. */
#if defined(__SAFE_C__)
#define _Owned [[clang::annotate_type("safec_owned")]]
#else /* plain clang: [[]] after _Nullable would not parse */
#define _Owned
#endif
/* On a field or global: points only at static data (string literals,
   globals, functions), like Rust's &'static. */
#if defined(__SAFE_C__)
#define _Static [[clang::annotate_type("safec_static")]]
#else /* plain clang: [[]] after _Nullable would not parse */
#define _Static
#endif
/* On a field or global: may point anywhere; dereferencing needs __unsafe. */
#if defined(__SAFE_C__)
#define _Unchecked [[clang::annotate_type("safec_unchecked")]]
#else /* plain clang: [[]] after _Nullable would not parse */
#define _Unchecked
#endif
/* On a parameter: an exclusive borrow, through which the function may take,
   free or replace owned fields (Rust's &mut). The caller's borrows of the
   argument end at the call. */
#if defined(__SAFE_C__)
#define _Exclusive [[clang::annotate_type("safec_excl")]]
#else
#define _Exclusive
#endif
/* On a struct: holds borrows, so it lives only in locals and parameters. */
#define _View __attribute__((annotate("safec_view")))
/* On a global: shared without a lock because it is written only before any
   thread starts; every write must be inside __unsafe. Rust's static mut. */
#define _Unsynchronized __attribute__((annotate("safec_unsynchronized")))
/* Move an owned pointer out of a field or global, leaving null (and a zero
   count): free(_Take(n->next)). Like Rust's Option::take. */
#if defined(__SAFE_C__)
#define _Take(lv) __builtin_safec_take(lv)
#else /* clang without Safe C has no builtin; lv is evaluated twice */
#define _Take(lv) ({ __typeof__(lv) __safec_t = (lv); (lv) = 0; __safec_t; })
#endif
#else
#define _Takes(N)
#define _Returns_owned
#define _Lifetimebound
#define _Noescape
#define _Owned
#define _Static
#define _Unchecked
#define _View
#define _Unsynchronized
#define _Exclusive
/* Evaluates lv twice; fine for the plain lvalues take is used on. */
#define _Take(lv) ({ __typeof__(lv) __safec_t = (lv); (lv) = 0; __safec_t; })
#endif

#if !defined(__clang__)
#define _Nonnull
#define _Nullable
#define _Null_unspecified
#endif


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

/* Automatic cleanup at the end of a scope, like Rust's Drop:
     string s _Drop(string_free) = {0};   calls string_free(&s)
     char *p _Drop_free = malloc(n);      calls free(p)
   A _Drop variable is never given away by name (that would free it twice);
   move it out with _Take(p), which leaves null for the cleanup. Clang and GCC
   both have the cleanup attribute; other compilers get no cleanup. */
#if defined(__GNUC__) || defined(__clang__)
/* __builtin_free so that safec.h needs no libc header. The annotation tells
   Safe C this void * parameter is a cleanup's variable, not a byte write. */
__attribute__((annotate("safec_cleanup")))
static inline void safec_drop_free_(void *pp) {
  /* unsafe: pp is the address of a pointer variable (or of a wide pointer,
     whose first field is the pointer); the cleanup only frees it. */
  __unsafe { __builtin_free(*(void **)pp); }
}
#define _Drop(fn) __attribute__((cleanup(fn)))
#define _Drop_free __attribute__((cleanup(safec_drop_free_)))
#else
#define _Drop(fn)
#define _Drop_free
#endif

#endif /* __SAFEC_H */
