/* Text to plain data. Include it once for each map, saying what to call it
   and what it holds:
     #define MAP_NAME counts
     #define MAP_VALUE int
     #include "map.h"
   That makes struct counts and these:
     struct counts m = {0};
     if (!counts_put(&m, STR("a"), 1)) ...        no memory
     int *_Nullable n = counts_get(&m, STR("a"));   null if it is not there
     counts_remove(&m, STR("a")); counts_free(&m);
   What counts_get returns ends at the next call that changes the map. Keys are
   copied. The hash is not keyed: do not let an attacker pick many keys. */
#if !defined(MAP_NAME) || !defined(MAP_VALUE)
#error "define MAP_NAME and MAP_VALUE before including map.h"
#endif
#include "text.h"

#ifndef SAFEC_MAP_HASH
#define SAFEC_MAP_HASH
static inline uint32_t map_hash_(struct str s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.len; i++)
    h = (h ^ (unsigned char)s.p[i]) * 16777619u;
  return h;
}
#endif

#define MAP_JOIN_(a, b) a##_##b
#define MAP_JOIN(a, b) MAP_JOIN_(a, b)
#define MAP_(f) MAP_JOIN(MAP_NAME, f)
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

/* Keys are kept end to end in one text; a slot says where its own is. */
struct MAP_(slot) {
  int state; /* 0 empty, 1 used, 2 removed */
  uint32_t hash;
  size_t at, len;
  MAP_VALUE value;
};
struct MAP_NAME {
  size_t cap;
  struct MAP_(slot) *_Owned _Nullable __counted_by(cap) p;
  size_t len, filled;
  struct text keys;
};
/* The key's slot, or cap. */
static inline size_t MAP_(find_)(const struct MAP_NAME *m, struct str key) {
  if (!m->p || m->cap == 0)
    return m->cap;
  uint32_t h = map_hash_(key);
  size_t i = h % m->cap;
  for (size_t n = 0; n < m->cap; n++) {
    if (m->p[i].state == 0)
      return m->cap;
    if (m->p[i].state == 1 && m->p[i].hash == h &&
        str_eq(str_part(text_str(&m->keys), m->p[i].at, m->p[i].len), key))
      return i;
    i = (i + 1) % m->cap;
  }
  return m->cap;
}
static inline int MAP_(grow_)(struct MAP_NAME *m) {
  if (m->len > SIZE_MAX / 8 / sizeof(struct MAP_(slot)))
    return 0;
  size_t cap = m->len * 4 + 8;
  struct MAP_(slot) *_Owned _Nullable bigger =
      calloc(cap, sizeof(struct MAP_(slot)));
  if (!bigger)
    return 0;
  for (size_t o = 0; m->p && o < m->cap; o++)
    if (m->p[o].state == 1) {
      size_t i = m->p[o].hash % cap;
      while (bigger[i].state)
        i = (i + 1) % cap;
      bigger[i] = m->p[o];
    }
  free(_Take(m->p));
  _Set(m->p, m->cap, bigger, cap);
  m->filled = m->len;
  return 1;
}
static inline MAP_VALUE *_Nullable MAP_(get)(struct MAP_NAME *m, struct str key) {
  size_t i = MAP_(find_)(m, key);
  if (!m->p || i >= m->cap)
    return NULL;
  return &m->p[i].value;
}
static inline int MAP_(put)(struct MAP_NAME *m, struct str key, MAP_VALUE value) {
  size_t i = MAP_(find_)(m, key);
  if (m->p && i < m->cap) {
    m->p[i].value = value;
    return 1;
  }
  if (m->filled >= m->cap / 2 && !MAP_(grow_)(m))
    return 0;
  size_t at = m->keys.len;
  if (!m->p || m->cap == 0 || !text_add(&m->keys, key.p, key.len))
    return 0;
  uint32_t h = map_hash_(key);
  i = h % m->cap;
  while (m->p[i].state)
    i = (i + 1) % m->cap;
  struct MAP_(slot) s = {1, h, at, key.len, value};
  m->p[i] = s;
  m->len++;
  m->filled++;
  return 1;
}
static inline int MAP_(remove)(struct MAP_NAME *m, struct str key) {
  size_t i = MAP_(find_)(m, key);
  if (!m->p || i >= m->cap)
    return 0;
  m->p[i].state = 2;
  m->len--;
  return 1;
}
static inline void MAP_(free)(struct MAP_NAME *m) {
  free(_Take(m->p));
  m->len = m->filled = 0;
  text_free(&m->keys);
}

#if defined(__clang__)
#pragma clang assume_nonnull end
#endif
#undef MAP_
#undef MAP_JOIN
#undef MAP_JOIN_
#undef MAP_NAME
#undef MAP_VALUE
