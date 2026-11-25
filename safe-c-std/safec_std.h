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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "safec.h"

/* Unannotated pointers are non-null, as Safe C assumes; say so to other
   clang versions too. */
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

/* ---- str: borrowed text ------------------------------------------------ */

/* Never null, like Rust's &str: the empty text is S(""). */
typedef struct str_ {
  const char *__counted_by(len) p;
  size_t len;
} str;

/* A string literal as a str: S("hello"). */
#define S(lit) ((str){(lit), sizeof(lit) - 1})

static inline str str_from(const char *s) {
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
static inline str str_slice(str s, size_t start, size_t n) {
  if (start > s.len)
    start = s.len;
  if (n > s.len - start)
    n = s.len - start;
  return (str){s.p + start, n};
}

static inline bool str_is_space_(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static inline str str_trim(str s) {
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

/* ---- SLICE(T): borrowed array, like Rust's &[T] ------------------------- */

/* typedef SLICE(int) int_slice; then int_slice s = {arr, n}. Never null;
   every index is checked against len. */
#define SLICE(T)                                                               \
  struct {                                                               \
    T *__counted_by(len) p;                                                    \
    size_t len;                                                                \
  }

/* ---- string: owned text ------------------------------------------------- */

typedef struct string_ {
  char *_Owned _Nullable __counted_by_or_null(cap) p;
  size_t len, cap;
} string;

static inline str string_view(const string *s) {
  if (!s->p)
    return S("");
  return str_slice((str){s->p, s->cap}, 0, s->len);
}

/* Makes room for n more bytes; returns false if out of memory. */
static inline bool string_reserve(string * s, size_t n) {
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

static inline bool string_push(string * s, str more) {
  if (!string_reserve(s, more.len))
    return false;
  if (!s->p)
    return more.len == 0;
  for (size_t i = 0; i < more.len; i++)
    s->p[s->len + i] = more.p[i];
  s->len += more.len;
  return true;
}

static inline void string_free(string * s) {
  free(_Take(s->p));
  s->len = 0;
}

/* ---- RESULT and OPTION: tagged unions (Rust's Result and Option) -------- */

/* RESULT(name, T, E) declares `struct name { bool ok; T value or E err; }`.
   Reading value without checking ok first is a compile error, and so is a
   switch on ok that forgets a case. TRY(r) returns r's error early. */
#define RESULT(name, T, E)                                                     \
  struct name {                                                                \
    bool ok;                                                                   \
    union _Tagged_by(ok) {                                                     \
      T value _When(true);                                                     \
      E err _When(false);                                                      \
    };                                                                         \
  }

#define OK(name, v) ((struct name){.ok = true, .value = (v)})
#define ERR(name, e) ((struct name){.ok = false, .err = (e)})

/* TRY(expr, name): like Rust's `?`. Evaluates expr (a result); on error,
   returns ERR(name, its err) from the current function, whose result type
   is struct name; otherwise gives the value. The error types must match.
     i64_result parse_two(str a, str b) {
       int64_t x = TRY(str_to_i64(a), i64_result);
       ...                                                          */
#define TRY(expr, name)                                                        \
  ({                                                                           \
    __typeof__(expr) safec_try_ = (expr);                                      \
    if (!safec_try_.ok)                                                        \
      return ERR(name, safec_try_.err);                                        \
    safec_try_.value;                                                          \
  })

/* OPTION(name, T): a value that may be absent, like Rust's Option<T>. */
#define OPTION(name, T)                                                        \
  struct name {                                                                \
    bool some;                                                                 \
    union _Tagged_by(some) {                                                   \
      T value _When(true);                                                     \
      char none_ _When(false);                                                 \
    };                                                                         \
  }

#define SOME(name, v) ((struct name){.some = true, .value = (v)})
#define NONE(name) ((struct name){.some = false, .none_ = 0})

/* Why a parse failed, and where: like a Rust enum with data in a variant. */
struct parse_error {
  enum parse_error_kind { PARSE_EMPTY, PARSE_BAD_CHAR, PARSE_OVERFLOW } kind;
  union _Tagged_by(kind) {
    struct { size_t at; char c; } bad _When(PARSE_BAD_CHAR);
  };
};

/* Parses a decimal integer. Fixed width: long is 32 bits on some platforms
   and 64 on others. */
typedef RESULT(i64_result, int64_t, struct parse_error) i64_result;

static inline i64_result str_to_i64(str s) {
  if (s.len == 0)
    return ERR(i64_result, (struct parse_error){.kind = PARSE_EMPTY});
  size_t i = 0;
  bool neg = s.p[0] == '-';
  if (neg || s.p[0] == '+')
    i = 1;
  if (i == s.len)
    return ERR(i64_result, (struct parse_error){.kind = PARSE_EMPTY});
  int64_t v = 0;
  for (; i < s.len; i++) {
    char c = s.p[i];
    if (c < '0' || c > '9')
      return ERR(i64_result, ((struct parse_error){
                                 .kind = PARSE_BAD_CHAR, .bad = {i, c}}));
    if (v > (INT64_MAX - (c - '0')) / 10)
      return ERR(i64_result, (struct parse_error){.kind = PARSE_OVERFLOW});
    v = v * 10 + (c - '0');
  }
  return OK(i64_result, neg ? -v : v);
}

/* ---- io ------------------------------------------------------------------ */

/* A path as a terminated string, for the C library. */
static inline bool string_terminate_(string * s) {
  if (!string_push(s, S("\0")))
    return false;
  s->len--; /* the terminator is not part of the text */
  return true;
}

/* Reads the whole file into *out (appending). Returns false on error. */
static inline bool read_file(str path, string * out) {
  string name __attribute__((cleanup(string_free))) = {0, 0, 0};
  if (!string_push(&name, path) || !string_terminate_(&name) || !name.p)
    return false;
  FILE *_Nullable f = fopen(name.p, "rb");
  if (!f)
    return false;
  bool ok = true;
  for (;;) {
    if (!string_reserve(out, 4096) || !out->p) {
      ok = false;
      break;
    }
    size_t got = fread(out->p + out->len, 1, out->cap - out->len, f);
    out->len += got;
    if (got == 0) {
      ok = !ferror(f);
      break;
    }
  }
  fclose(f);
  return ok;
}

/* Writes all of bytes to fd, retrying short writes. */
static inline bool write_all(int fd, str bytes) {
  size_t done = 0;
  while (done < bytes.len) {
    ssize_t n = write(fd, bytes.p + done, bytes.len - done);
    if (n <= 0)
      return false;
    done += (size_t)n;
  }
  return true;
}

/* The lines of a file, read into one reused buffer. */
typedef struct lines_ {
  FILE *_Owned _Nullable f;
  string line;
} lines;

static inline bool lines_open(str path, lines * it) {
  string name __attribute__((cleanup(string_free))) = {0, 0, 0};
  if (!string_push(&name, path) || !string_terminate_(&name) || !name.p)
    return false;
  it->f = fopen(name.p, "rb");
  it->line.len = 0;
  return it->f != NULL;
}

/* Reads the next line; false at the end of the file. */
static inline bool lines_next(lines * it) {
  if (!it->f)
    return false;
  it->line.len = 0;
  int c;
  bool any = false;
  while ((c = fgetc(it->f)) != EOF) {
    any = true;
    if (c == '\n')
      break;
    char ch = (char)c;
    if (!string_push(&it->line, (str){&ch, 1}))
      return false;
  }
  return any;
}

/* The line lines_next read, without its newline. It borrows the iterator's
   buffer, so it is valid until the next lines_next. */
static inline str lines_line(const lines *it) {
  return string_view(&it->line);
}

static inline void lines_close(lines * it) {
  FILE *_Nullable f = _Take(it->f);
  if (f)
    fclose(f);
  string_free(&it->line);
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
      __typeof__(*(v)->items) *_Nullable _Owned safec_new_ =                   \
          calloc(safec_cap_, sizeof(*(v)->items));                             \
      if (!safec_new_)                                                         \
        abort();                                                               \
      /* unsafe: moves the elements (owners among them) byte for byte into the \
         new zeroed block; the old one is freed without touching them */       \
      if ((v)->items)                                                          \
        __unsafe {                                                             \
          memcpy(safec_new_, (v)->items, safec_len_ * sizeof(*(v)->items));    \
        }                                                                      \
      free(_Take((v)->items));                                                 \
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
