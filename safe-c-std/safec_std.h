/* safec_std.h: a small standard library written in Safe C.
 *
 * str     borrowed text: pointer and length, no terminator needed (&str)
 * string  owned, growable text (String)
 * VEC(T)  owned, growable array (Vec<T>)
 *
 * Header-only. It is plain C: on compilers without Safe C the annotations in
 * safec.h expand to nothing. Under Safe C every index is bounds-checked, a
 * view used after its string is freed is a compile error, and each buffer is
 * freed exactly once.
 */
#ifndef SAFEC_STD_H
#define SAFEC_STD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "safec.h"

/* Unannotated pointers are non-null, as Safe C assumes; say so to other
   clang versions too. */
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

/* ---- str: borrowed text ------------------------------------------------ */

/* Never null, like Rust's &str: the empty text is S(""). */
typedef struct _View str_ {
  const char *__counted_by(len) p;
  size_t len;
} str;

/* A string literal as a str: S("hello"). */
#define S(lit) ((str){(lit), sizeof(lit) - 1})

static inline str str_from(const char *s _Lifetimebound) {
  size_t n = strlen(s);
  return (str){__null_terminated_to_indexable(s), n};
}

static inline bool str_eq(str a, str b) {
  if (a.len != b.len)
    return false;
  for (size_t i = 0; i < a.len; i++)
    if (a.p[i] != b.p[i])
      return false;
  return true;
}

/* The part of s from start, at most n bytes. */
static inline str str_slice(str s _Lifetimebound, size_t start, size_t n) {
  if (start > s.len)
    start = s.len;
  if (n > s.len - start)
    n = s.len - start;
  return (str){s.p + start, n};
}

static inline bool str_is_space_(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline str str_trim(str s _Lifetimebound) {
  size_t b = 0, e = s.len;
  while (b < e && str_is_space_(s.p[b]))
    b++;
  while (e > b && str_is_space_(s.p[e - 1]))
    e--;
  return str_slice(s, b, e - b);
}

/* Splits off the text before the next sep: *out gets it, *rest the text
   after. Returns false when rest is empty. */
static inline bool str_split(str *rest, char sep, str *out) {
  if (rest->len == 0)
    return false;
  size_t i = 0;
  while (i < rest->len && rest->p[i] != sep)
    i++;
  *out = str_slice(*rest, 0, i);
  *rest = str_slice(*rest, i < rest->len ? i + 1 : i, rest->len);
  return true;
}

/* ---- string: owned text ------------------------------------------------- */

typedef struct string_ {
  char *_Owned _Nullable __counted_by_or_null(cap) p;
  size_t len, cap;
} string;

static inline str string_view(const string *s _Lifetimebound) {
  if (!s->p)
    return S("");
  return str_slice((str){s->p, s->cap}, 0, s->len);
}

/* Makes room for n more bytes; returns false if out of memory. */
static inline bool string_reserve(string *_Excl s, size_t n) {
  if (s->cap - s->len >= n)
    return true;
  size_t cap = s->cap ? s->cap : 16;
  while (cap - s->len < n)
    cap *= 2;
  size_t keep = s->len;
  char *_Nullable _Owned grown = realloc(_Take(s->p), cap);
  if (!grown)
    return false; /* realloc failed: _Take left s empty, so nothing leaks */
  s->p = grown;
  s->cap = cap;
  s->len = keep;
  return true;
}

static inline bool string_push(string *_Excl s, str more) {
  if (!string_reserve(s, more.len))
    return false;
  if (!s->p)
    return more.len == 0;
  for (size_t i = 0; i < more.len; i++)
    s->p[s->len + i] = more.p[i];
  s->len += more.len;
  return true;
}

static inline void string_free(string *_Excl s) {
  free(_Take(s->p));
  s->len = 0;
}

/* ---- VEC(T): owned, growable array -------------------------------------- */

#define VEC(T)                                                                 \
  struct {                                                                     \
    T *_Owned _Nullable __counted_by_or_null(cap) items;                       \
    size_t len, cap;                                                           \
  }

/* Appends x; aborts if out of memory (like Rust's Vec::push). */
#define vec_push(v, x)                                                         \
  do {                                                                         \
    if ((v)->len == (v)->cap) {                                                \
      size_t safec_cap_ = (v)->cap ? 2 * (v)->cap : 4;                         \
      size_t safec_len_ = (v)->len;                                            \
      __typeof__(*(v)->items) *_Nullable _Owned safec_new_ = realloc(          \
          _Take((v)->items), safec_cap_ * sizeof(*(v)->items));                \
      if (!safec_new_)                                                         \
        abort();                                                               \
      (v)->items = safec_new_;                                                 \
      (v)->cap = safec_cap_;                                                   \
      (v)->len = safec_len_;                                                   \
    }                                                                          \
    if ((v)->items)                                                            \
      (v)->items[(v)->len++] = (x);                                            \
  } while (0)

/* for each element: vec_each(int n, nums) printf("%d\n", n); */
#define vec_each(decl, v)                                                      \
  for (size_t safec_i_ = 0; (v).items && safec_i_ < (v).len; safec_i_++)      \
    for (int safec_once_ = 1; safec_once_;)                                    \
      for (decl = (v).items[safec_i_]; safec_once_; safec_once_ = 0)

#define vec_free(v)                                                            \
  do {                                                                         \
    free(_Take((v)->items));                                                   \
    (v)->len = 0;                                                              \
  } while (0)

#if defined(__clang__)
#pragma clang assume_nonnull end
#endif

#endif /* SAFEC_STD_H */
