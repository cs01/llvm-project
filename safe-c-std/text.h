/* Text without strtok, strchr or strcpy. No __unsafe, and plain C too.

     str   text someone else owns: a pointer and a length, for locals and parameters
     text  text this struct owns: it grows, and text_free frees it */
#ifndef SAFEC_TEXT_H
#define SAFEC_TEXT_H
#include "result.h"
#include "safec.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

struct _View str {
  const char *__counted_by(len) p;
  size_t len;
};
#define STR(literal) ((struct str){(literal), sizeof(literal) - 1})

static inline int str_eq(struct str a, struct str b) {
  return a.len == b.len && (a.len == 0 || memcmp(a.p, b.p, a.len) == 0);
}

/* Below, at or above 0, in byte order; a shorter text comes first. */
static inline int str_cmp(struct str a, struct str b) {
  size_t n = a.len < b.len ? a.len : b.len;
  int r = n ? memcmp(a.p, b.p, n) : 0;
  return r ? r : (a.len > b.len) - (a.len < b.len);
}

/* Writes s, then the end of line if there is one: 1, or 0 if it could not. */
static inline int str_write(struct str s, const char *__null_terminated end, FILE *f) {
  return fwrite(s.p, 1, s.len, f) == s.len && fputs(end, f) >= 0;
}

/* Where c is, or s.len. */
static inline size_t str_find(struct str s, char c) {
  size_t i = 0;
  while (i < s.len && s.p[i] != c)
    i++;
  return i;
}

/* n bytes from 'from', or what there is. */
static inline struct str str_part(struct str s, size_t from, size_t n) {
  if (from > s.len)
    from = s.len;
  if (n > s.len - from)
    n = s.len - from;
  struct str r = {s.p + from, n};
  return r;
}

/* Splitting:
     for (struct str rest = s; rest.len; rest = str_after(rest, ','))
       use(str_before(rest, ',')); */
static inline struct str str_before(struct str s, char c) {
  return str_part(s, 0, str_find(s, c));
}
static inline struct str str_after(struct str s, char c) {
  return str_part(s, str_find(s, c) + 1, s.len);
}

static inline struct str str_trim(struct str s) {
  size_t b = 0, e = s.len;
  while (b < e && (s.p[b] == ' ' || s.p[b] == '\t' || s.p[b] == '\r' || s.p[b] == '\n'))
    b++;
  while (e > b && (s.p[e - 1] == ' ' || s.p[e - 1] == '\t' || s.p[e - 1] == '\r' || s.p[e - 1] == '\n'))
    e--;
  return str_part(s, b, e - b);
}

/* All of s as a number. */
enum str_number_error { STR_EMPTY, STR_NOT_A_NUMBER, STR_TOO_BIG };
typedef RESULT(str_long, long, enum str_number_error) str_long;
static inline str_long str_int(struct str s) {
  size_t i = s.len > 0 && s.p[0] == '-';
  int minus = i == 1;
  long v = 0;
  if (s.len == 0)
    return ERR(str_long, STR_EMPTY);
  if (i == s.len)
    return ERR(str_long, STR_NOT_A_NUMBER);
  for (; i < s.len; i++) {
    int d = s.p[i] - '0';
    if (d < 0 || d > 9)
      return ERR(str_long, STR_NOT_A_NUMBER);
    if (v < (LONG_MIN + d) / 10)
      return ERR(str_long, STR_TOO_BIG);
    v = v * 10 - d; /* counts down: there is one more negative number */
  }
  if (!minus && v == LONG_MIN)
    return ERR(str_long, STR_TOO_BIG);
  return OK(str_long, minus ? v : -v);
}

struct text {
  size_t cap;
  char *_Owned _Nullable __counted_by(cap) p;
  size_t len;
};

static inline struct str text_str(const struct text *t) {
  if (!t->p)
    return STR("");
  struct str all = {t->p, t->cap};
  return str_part(all, 0, t->len);
}

static inline void text_free(struct text *_Empties t) {
  free(_Take(t->p));
  t->len = 0;
}

/* Adds n bytes: 1, or 0 and no change if there is no memory. */
static inline int text_add(struct text *t, const char *__counted_by(n) from, size_t n) {
  if (n > SIZE_MAX / 4 || t->len > SIZE_MAX / 4)
    return 0;
  size_t need = t->len + n;
  if (!t->p || need > t->cap) {
    size_t cap = need * 2 + 16;
    char *_Owned _Nullable bigger = calloc(cap, 1);
    if (!bigger)
      return 0;
    if (t->p && t->len <= t->cap)
      memcpy(bigger, t->p, t->len);
    free(_Take(t->p));
    _Set(t->p, t->cap, bigger, cap);
  }
  if (!t->p)
    return 0;
  memcpy(t->p + t->len, from, n);
  t->len = need;
  return 1;
}

/* Replaces it with from: 1, or 0 and empty if there is no memory. */
static inline int text_set(struct text *t, struct str from) {
  t->len = 0;
  return text_add(t, from.p, from.len);
}

/* Replaces n bytes at 'at' with others: 1, or 0 and no change. */
static inline int text_splice(struct text *t, size_t at, size_t n, struct str with) {
  if (at > t->len || n > t->len - at)
    return 0;
  size_t old = t->len, tail = old - at - n;
  /* Adding it first makes room, and is all there is to do at the end. */
  if (!text_add(t, with.p, with.len) || !t->p)
    return 0;
  memmove(t->p + at + with.len, t->p + at + n, tail);
  memcpy(t->p + at, with.p, with.len);
  t->len = old - n + with.len;
  return 1;
}

/* The next line, without its '\n': 1, or 0 at the end. */
static inline int text_line(struct text *t, FILE *f) {
  char part[256];
  t->len = 0;
  for (int some = 0;; some = 1) {
    /* A '\n' with the 0 fgets adds after it was read; any other is filler. */
    memset(part, '\n', sizeof part);
    if (!fgets(part, sizeof part, f))
      return some;
    struct str all = {part, sizeof part};
    size_t n = str_find(all, '\n');
    if (n == sizeof part)
      n = sizeof part - 1; /* no end of line yet */
    else if (n + 1 < sizeof part && part[n + 1] == 0)
      return text_add(t, part, n);
    else
      return n > 0 && text_add(t, part, n - 1); /* the end of the file */
    if (!text_add(t, part, n))
      return 0;
  }
}

/* A growing list of strings: all the bytes in one text, and where each is.
     struct strings names = {0};
     if (!strings_add(&names, STR("Ada"))) ...      no memory
     struct str s = strings_at(&names, 0);          empty if there is no such one
     strings_count(&names); strings_free(&names);
   What strings_at returns ends at the next strings_add or strings_free. */
struct strings_span_ {
  size_t at, len;
};
struct strings {
  struct text bytes;
  size_t cap;
  struct strings_span_ *_Owned _Nullable __counted_by(cap) where;
  size_t count;
};

static inline size_t strings_count(const struct strings *s) {
  return s->count;
}

static inline int strings_add(struct strings *s, struct str x) {
  if (!s->where || s->count >= s->cap) {
    if (s->count > SIZE_MAX / 4 / sizeof(struct strings_span_))
      return 0;
    size_t cap = s->count * 2 + 8;
    struct strings_span_ *_Owned _Nullable bigger = calloc(cap, sizeof(struct strings_span_));
    if (!bigger)
      return 0;
    if (s->where && s->count <= s->cap)
      memcpy(bigger, s->where, s->count * sizeof(struct strings_span_));
    free(_Take(s->where));
    _Set(s->where, s->cap, bigger, cap);
  }
  size_t at = s->bytes.len;
  if (!s->where || !text_add(&s->bytes, x.p, x.len))
    return 0;
  struct strings_span_ w = {at, x.len};
  s->where[s->count] = w;
  s->count++;
  return 1;
}

static inline struct str strings_at(const struct strings *s, size_t i) {
  if (!s->where || i >= s->count || i >= s->cap)
    return STR("");
  return str_part(text_str(&s->bytes), s->where[i].at, s->where[i].len);
}

static inline void strings_free(struct strings *_Empties s) {
  text_free(&s->bytes);
  free(_Take(s->where));
  s->count = 0;
}

#if defined(__clang__)
#pragma clang assume_nonnull end
#endif
#endif
