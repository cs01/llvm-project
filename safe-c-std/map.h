/* Text to plain data.
     MAP(counts, int);
     struct counts m = {0};
     if (!counts_put(&m, STR("a"), 1)) ...        no memory
     int *_Nullable n = counts_get(&m, STR("a"));   null if it is not there
     counts_free(&m);
   What counts_get returns ends at the next call that changes the map. Keys are
   copied. The hash is not keyed: do not let an attacker pick many keys. */
#ifndef SAFEC_MAP_H
#define SAFEC_MAP_H
#include "text.h"
#include "vec.h"

static inline uint32_t map_hash_(struct str s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.len; i++)
    h = (h ^ (unsigned char)s.p[i]) * 16777619u;
  return h;
}

/* Keys are kept end to end in one text; a slot says where its own is. */
#define MAP(name, V)                                                           \
  VEC_NONNULL_BEGIN_                                                           \
  struct name##_slot {                                                         \
    int state; /* 0 empty, 1 used, 2 removed */                                \
    uint32_t hash;                                                             \
    size_t at, len;                                                            \
    V value;                                                                   \
  };                                                                           \
  struct name {                                                                \
    size_t cap;                                                                \
    struct name##_slot *_Owned _Nullable __counted_by(cap) p;                  \
    size_t len, filled;                                                        \
    struct text keys;                                                          \
  };                                                                           \
  /* The key's slot, or cap. */                                                \
  static inline size_t name##_find_(const struct name *m, struct str key) {    \
    if (!m->p || m->cap == 0)                                                  \
      return m->cap;                                                           \
    uint32_t h = map_hash_(key);                                               \
    size_t i = h % m->cap;                                                     \
    for (size_t n = 0; n < m->cap; n++) {                                      \
      if (m->p[i].state == 0)                                                  \
        return m->cap;                                                         \
      if (m->p[i].state == 1 && m->p[i].hash == h &&                           \
          str_eq(str_part(text_str(&m->keys), m->p[i].at, m->p[i].len), key))  \
        return i;                                                              \
      i = (i + 1) % m->cap;                                                    \
    }                                                                          \
    return m->cap;                                                             \
  }                                                                            \
  static inline int name##_grow_(struct name *m) {                             \
    if (m->len > SIZE_MAX / 8 / sizeof(struct name##_slot))                    \
      return 0;                                                                \
    size_t cap = m->len * 4 + 8;                                               \
    struct name##_slot *_Owned _Nullable bigger =                              \
        calloc(cap, sizeof(struct name##_slot));                               \
    if (!bigger)                                                               \
      return 0;                                                                \
    for (size_t o = 0; m->p && o < m->cap; o++)                                \
      if (m->p[o].state == 1) {                                                \
        size_t i = m->p[o].hash % cap;                                         \
        while (bigger[i].state)                                                \
          i = (i + 1) % cap;                                                   \
        bigger[i] = m->p[o];                                                   \
      }                                                                        \
    free(_Take(m->p));                                                         \
    _Set(m->p, m->cap, bigger, cap);                                           \
    m->filled = m->len;                                                        \
    return 1;                                                                  \
  }                                                                            \
  static inline V *_Nullable name##_get(struct name *m, struct str key) {      \
    size_t i = name##_find_(m, key);                                           \
    if (!m->p || i >= m->cap)                                                  \
      return NULL;                                                             \
    return &m->p[i].value;                                                     \
  }                                                                            \
  static inline int name##_put(struct name *m, struct str key, V value) {      \
    size_t i = name##_find_(m, key);                                           \
    if (m->p && i < m->cap) {                                                  \
      m->p[i].value = value;                                                   \
      return 1;                                                                \
    }                                                                          \
    if (m->filled >= m->cap / 2 && !name##_grow_(m))                           \
      return 0;                                                                \
    size_t at = m->keys.len;                                                   \
    if (!m->p || m->cap == 0 || !text_add(&m->keys, key.p, key.len))           \
      return 0;                                                                \
    uint32_t h = map_hash_(key);                                               \
    i = h % m->cap;                                                            \
    while (m->p[i].state)                                                      \
      i = (i + 1) % m->cap;                                                    \
    struct name##_slot s = {1, h, at, key.len, value};                         \
    m->p[i] = s;                                                               \
    m->len++;                                                                  \
    m->filled++;                                                               \
    return 1;                                                                  \
  }                                                                            \
  static inline int name##_remove(struct name *m, struct str key) {            \
    size_t i = name##_find_(m, key);                                           \
    if (!m->p || i >= m->cap)                                                  \
      return 0;                                                                \
    m->p[i].state = 2;                                                         \
    m->len--;                                                                  \
    return 1;                                                                  \
  }                                                                            \
  static inline void name##_free(struct name *m) {                             \
    free(_Take(m->p));                                                         \
    m->len = m->filled = 0;                                                    \
    text_free(&m->keys);                                                       \
  }                                                                            \
  VEC_NONNULL_END_                                                             \
  struct name
#endif
