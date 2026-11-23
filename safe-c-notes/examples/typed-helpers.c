// How Safe C keeps void* out of everyday code: the unsafe casts live once, in a
// library macro; user code only sees typed functions.
//
//   clang -fsafe-c typed-helpers.c && ./a.out
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// 1. A growable array, generated per element type (like Vec<T>).
//    The bounds annotation ties `data` to `cap`, so every access is checked.
// ---------------------------------------------------------------------------
#define SAFEC_VEC(T, Name)                                                     \
  typedef struct {                                                             \
    T *__counted_by_or_null(cap) data;                                         \
    size_t len;                                                                \
    size_t cap;                                                                \
  } Name;                                                                      \
  static inline int Name##_push(Name *v, T x) {                                \
    if (v->len == v->cap) {                                                    \
      size_t ncap = v->cap ? v->cap * 2 : 8;                                   \
      T *_Nullable p = realloc(v->data, ncap * sizeof(T));                     \
      if (!p)                                                                  \
        return -1;                                                             \
      v->data = p;                                                             \
      v->cap = ncap;                                                           \
    }                                                                          \
    v->data[v->len++] = x;                                                     \
    return 0;                                                                  \
  }                                                                            \
  static inline T Name##_get(const Name *v, size_t i) { return v->data[i]; }

SAFEC_VEC(int, IntVec)
SAFEC_VEC(double, DoubleVec)

// ---------------------------------------------------------------------------
// 2. A callback with a context pointer, typed.
//    C APIs take `void (*fn)(void *ctx, int item), void *ctx`: nothing stops you
//    passing a ctx of the wrong type. The macro checks at compile time that the
//    callback's first parameter and the context have the same type, then does
//    the one void* conversion itself.
// ---------------------------------------------------------------------------

// A plain C API we do not control.
static void each_item(const int *__counted_by(n) items, size_t n,
                      void (*fn)(void *ctx, int item), void *ctx) {
  for (size_t i = 0; i < n; i++)
    fn(ctx, items[i]);
}

#define EACH_ITEM(items, n, fn, ctx)                                           \
  do {                                                                         \
    /* Compile-time check: fn must take a pointer to ctx's type first. */     \
    void (*checked_)(typeof(*(ctx)) *, int) = (fn);                            \
    (void)checked_;                                                            \
    __unsafe { each_item((items), (n), (void (*)(void *, int))(fn), (ctx)); }  \
  } while (0)

struct Totals { long sum; int count; };

static void add(struct Totals *t, int item) {
  t->sum += item;
  t->count++;
}

int main(void) {
  IntVec v = {0};
  for (int i = 1; i <= 20; i++)
    if (IntVec_push(&v, i) != 0)
      return 1;

  struct Totals t = {0, 0};
  EACH_ITEM(v.data, v.len, add, &t);    // ok: add takes struct Totals *
  // EACH_ITEM(v.data, v.len, add, &v); // compile error: add does not take IntVec *

  printf("sum=%ld count=%d third=%d\n", t.sum, t.count, IntVec_get(&v, 2));
  // IntVec_get(&v, 100) would trap: index 100 is past cap.
  free(v.data);
  return 0;
}
