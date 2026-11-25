/* pool.h: handles that know when they are stale.

     POOL(conn, struct conn)            defines conn_pool, conn_id and functions
     conn_pool p = {0};
     conn_id a;
     if (!conn_add(&p, &a)) ...         a new zeroed slot; false if out of memory
     struct conn *_Nullable c = conn_get(&p, a);      null once a was removed
     conn_remove(&p, a);                false if it was already gone
     conn_free(&p);

   Keep handles wherever a pointer would be a stored borrow (a cache, a graph, a
   back pointer). A slot's generation changes when it is removed, and a slot
   whose generation would wrap is not used again, so a stale handle is never
   mistaken for a new one of the same pool. A handle does not say which pool it
   is from. T holds no owners: a slot is reused as bytes. */
#ifndef SAFEC_POOL_H
#define SAFEC_POOL_H
#include "safec.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define POOL(name, T)                                                          \
  typedef struct { uint32_t slot, gen; } name##_id;                            \
  typedef struct { uint32_t gen; uint32_t next_free; T value; } name##_slot;   \
  typedef struct {                                                             \
    uint32_t cap;                                                              \
    name##_slot *_Owned _Nullable __counted_by(cap) slots;                     \
    uint32_t used, first_free; /* 1 + a slot's number; 0 is none */            \
  } name##_pool;                                                               \
  /* Odd generations are live. */                                              \
  static inline T *_Nullable name##_get(name##_pool *p, name##_id id) {        \
    if (!p->slots || id.slot >= p->used || !(id.gen & 1))                      \
      return NULL;                                                             \
    name##_slot *s = &p->slots[id.slot];                                       \
    if (s->gen != id.gen)                                                      \
      return NULL;                                                             \
    return &s->value;                                                          \
  }                                                                            \
  static inline bool name##_grow(name##_pool *p) {                             \
    if (p->cap > UINT32_MAX / 4)                                               \
      return false;                                                            \
    uint32_t cap = p->cap ? p->cap * 2 : 8;                                    \
    name##_slot *_Owned _Nullable n = calloc(cap, sizeof *n);                  \
    if (!n)                                                                    \
      return false;                                                            \
    if (p->slots)                                                              \
      for (uint32_t i = 0; i < p->used; i++)                                   \
        n[i] = p->slots[i];                                                    \
    free(_Take(p->slots));                                                     \
    p->slots = n;                                                              \
    p->cap = cap;                                                              \
    return true;                                                               \
  }                                                                            \
  static inline bool name##_add(name##_pool *p, name##_id *out) {              \
    uint32_t i = p->first_free;                                                \
    if (i == 0 && p->used == p->cap && !name##_grow(p))                        \
      return false;                                                            \
    if (!p->slots)                                                             \
      return false;                                                            \
    if (i) {                                                                   \
      i--;                                                                     \
      p->first_free = p->slots[i].next_free;                                   \
      memset(&p->slots[i].value, 0, sizeof p->slots[i].value);                 \
    } else {                                                                   \
      i = p->used++;                                                           \
    }                                                                          \
    p->slots[i].gen++;                                                         \
    out->slot = i;                                                             \
    out->gen = p->slots[i].gen;                                                \
    return true;                                                               \
  }                                                                            \
  static inline bool name##_remove(name##_pool *p, name##_id id) {             \
    if (!name##_get(p, id) || !p->slots)                                       \
      return false;                                                            \
    name##_slot *s = &p->slots[id.slot];                                       \
    s->gen++;                                                                  \
    if (s->gen != 0) {                                                         \
      s->next_free = p->first_free;                                            \
      p->first_free = id.slot + 1;                                             \
    }                                                                          \
    return true;                                                               \
  }                                                                            \
  static inline void name##_free(name##_pool *p) {                             \
    free(_Take(p->slots));                                                     \
    p->slots = NULL;                                                           \
    p->cap = 0;                                                                \
    p->used = 0;                                                               \
    p->first_free = 0;                                                         \
  }                                                                            \
  struct name##_pool_needs_a_semicolon

#endif
