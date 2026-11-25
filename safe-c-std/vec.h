/* A growing array of plain data.
     VEC(ints, int);
     struct ints v = {0};
     if (!ints_add(&v, 4)) ...          no memory
     int *_Nullable p = ints_at(&v, 0);   null if there is no such element
     ints_free(&v);
   What ints_at returns ends at the next ints_add, ints_pop or ints_free. */
#ifndef SAFEC_VEC_H
#define SAFEC_VEC_H
#include "safec.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__)
#define VEC_NONNULL_BEGIN_ _Pragma("clang assume_nonnull begin")
#define VEC_NONNULL_END_ _Pragma("clang assume_nonnull end")
#else
#define VEC_NONNULL_BEGIN_
#define VEC_NONNULL_END_
#endif

#define VEC(name, T)                                                           \
  VEC_NONNULL_BEGIN_                                                           \
  struct name {                                                                \
    size_t cap;                                                                \
    T *_Owned _Nullable __counted_by(cap) p;                                   \
    size_t len;                                                                \
  };                                                                           \
  static inline int name##_add(struct name *v, T x) {                          \
    if (!v->p || v->len >= v->cap) {                                           \
      if (v->len > SIZE_MAX / 4 / sizeof(T))                                   \
        return 0;                                                              \
      size_t cap = v->len * 2 + 8;                                             \
      T *_Owned _Nullable bigger = calloc(cap, sizeof(T));                     \
      if (!bigger)                                                             \
        return 0;                                                              \
      if (v->p && v->len <= v->cap)                                            \
        memcpy(bigger, v->p, v->len * sizeof(T));                              \
      free(_Take(v->p));                                                       \
      _Set(v->p, v->cap, bigger, cap);                                         \
    }                                                                          \
    if (!v->p)                                                                 \
      return 0;                                                                \
    v->p[v->len] = x;                                                          \
    v->len++;                                                                  \
    return 1;                                                                  \
  }                                                                            \
  static inline T *_Nullable name##_at(struct name *v, size_t i) {             \
    if (!v->p || i >= v->len || i >= v->cap)                                   \
      return NULL;                                                             \
    return &v->p[i];                                                           \
  }                                                                            \
  static inline int name##_pop(struct name *v, T *out) {                       \
    if (!v->p || v->len == 0)                                                  \
      return 0;                                                                \
    v->len--;                                                                  \
    *out = v->p[v->len];                                                       \
    return 1;                                                                  \
  }                                                                            \
  static inline void name##_free(struct name *v) {                             \
    free(_Take(v->p));                                                         \
    v->len = 0;                                                                \
  }                                                                            \
  VEC_NONNULL_END_                                                             \
  struct name
#endif
