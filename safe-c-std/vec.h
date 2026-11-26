/* A growing array of plain data. Include it once for each type, saying what
   to call it and what it holds:
     #define VEC_NAME ints
     #define VEC_TYPE int
     #include "vec.h"
   That makes struct ints and these, all checked in bounds:
     struct ints v = {0};
     if (!ints_add(&v, 4)) ...                 no memory
     int *_Nullable p = ints_at(&v, 0);        null if there is no such element
     int x; if (ints_get(&v, 0, &x)) ...       a copy
     ints_set(&v, 0, 5); ints_pop(&v, &x); ints_free(&v);
   What ints_at returns ends at the next call that changes v.

   For owners (strings from strdup, structs from malloc), also say how to see
   one without owning it:
     #define VEC_NAME names
     #define VEC_TYPE char *_Owned _Nullable __null_terminated
     #define VEC_BORROW char *_Nullable __null_terminated
     #define VEC_OWNED 1
     #include "vec.h"
   Then names_add takes the string (and frees it if there is no memory),
   names_at gives a borrow of one, names_take moves one out (leaving null),
   names_pop moves the last out, and names_free frees each and the array.
   VEC_FREE(x) says how to free one, if not free(x). */
#if !defined(VEC_NAME) || !defined(VEC_TYPE)
#error "define VEC_NAME and VEC_TYPE before including vec.h"
#endif
#if defined(VEC_OWNED) && VEC_OWNED && !defined(VEC_BORROW)
#error "an owned VEC_TYPE needs VEC_BORROW: the same pointer, not _Owned"
#endif
#ifndef VEC_FREE
#define VEC_FREE(x) free(x)
#endif
#include "safec.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VEC_JOIN_(a, b) a##_##b
#define VEC_JOIN(a, b) VEC_JOIN_(a, b)
#define VEC_(f) VEC_JOIN(VEC_NAME, f)
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

struct VEC_NAME {
  size_t cap;
  VEC_TYPE *_Owned _Nullable __counted_by(cap) p;
  size_t len;
};

#if !defined(VEC_OWNED) || !VEC_OWNED
static inline int VEC_(add)(struct VEC_NAME *v, VEC_TYPE x) {
  if (!v->p || v->len >= v->cap) {
    if (v->len > SIZE_MAX / 4 / sizeof(VEC_TYPE))
      return 0;
    size_t cap = v->len * 2 + 8;
    VEC_TYPE *_Owned _Nullable bigger = calloc(cap, sizeof(VEC_TYPE));
    if (!bigger)
      return 0;
    if (v->p && v->len <= v->cap)
      memcpy(bigger, v->p, v->len * sizeof(VEC_TYPE));
    free(_Take(v->p));
    _Set(v->p, v->cap, bigger, cap);
  }
  if (!v->p)
    return 0;
  v->p[v->len] = x;
  v->len++;
  return 1;
}

static inline VEC_TYPE *_Nullable VEC_(at)(struct VEC_NAME *v, size_t i) {
  if (!v->p || i >= v->len || i >= v->cap)
    return NULL;
  return &v->p[i];
}

static inline int VEC_(get)(const struct VEC_NAME *v, size_t i, VEC_TYPE *out) {
  if (!v->p || i >= v->len || i >= v->cap)
    return 0;
  *out = v->p[i];
  return 1;
}

static inline int VEC_(set)(struct VEC_NAME *v, size_t i, VEC_TYPE x) {
  if (!v->p || i >= v->len || i >= v->cap)
    return 0;
  v->p[i] = x;
  return 1;
}

static inline int VEC_(pop)(struct VEC_NAME *v, VEC_TYPE *out) {
  if (!v->p || v->len == 0)
    return 0;
  v->len--;
  *out = v->p[v->len];
  return 1;
}

static inline void VEC_(free)(struct VEC_NAME *_Empties v) {
  free(_Take(v->p));
  v->len = 0;
}

#else
/* Owners: moved one at a time, since copying an owner's bytes would make two. */
static inline int VEC_(grow_)(struct VEC_NAME *v) {
  if (v->len > SIZE_MAX / 4 / sizeof(VEC_TYPE))
    return 0;
  size_t cap = v->len * 2 + 8;
  VEC_TYPE *_Owned _Nullable bigger = calloc(cap, sizeof(VEC_TYPE));
  if (!bigger)
    return 0;
  for (size_t i = 0; v->p && i < v->len && i < v->cap; i++)
    bigger[i] = _Take(v->p[i]);
  free(_Take(v->p));
  _Set(v->p, v->cap, bigger, cap);
  return 1;
}

/* Takes x: it is in v, or freed if there is no memory. */
static inline int VEC_(add)(struct VEC_NAME *v, VEC_TYPE x) {
  if ((!v->p || v->len >= v->cap) && !VEC_(grow_)(v)) {
    VEC_FREE(x);
    return 0;
  }
  if (!v->p) {
    VEC_FREE(x);
    return 0;
  }
  v->p[v->len] = x;
  v->len++;
  return 1;
}

/* One element, borrowed: null if there is no such element, or it was taken. */
static inline VEC_BORROW VEC_(at)(const struct VEC_NAME *v, size_t i) {
  if (!v->p || i >= v->len || i >= v->cap)
    return NULL;
  VEC_BORROW x = v->p[i];
  return x;
}

/* Moves element i out, leaving null in its place. */
static inline VEC_TYPE VEC_(take)(struct VEC_NAME *v, size_t i) {
  if (!v->p || i >= v->len || i >= v->cap)
    return NULL;
  return _Take(v->p[i]);
}

/* Moves the last element out, or null if there is none. */
static inline VEC_TYPE VEC_(pop)(struct VEC_NAME *v) {
  if (!v->p || v->len == 0 || v->len > v->cap)
    return NULL;
  v->len--;
  return _Take(v->p[v->len]);
}

static inline void VEC_(free)(struct VEC_NAME *_Empties v) {
  for (size_t i = 0; v->p && i < v->len && i < v->cap; i++)
    VEC_FREE(_Take(v->p[i]));
  free(_Take(v->p));
  v->len = 0;
}
#endif

#if defined(__clang__)
#pragma clang assume_nonnull end
#endif
#undef VEC_
#undef VEC_JOIN
#undef VEC_JOIN_
#undef VEC_NAME
#undef VEC_TYPE
#undef VEC_BORROW
#undef VEC_OWNED
#undef VEC_FREE
