/* Growing arrays and maps. */
#include "../text.h"
struct pair { int a; double b; };
#define VEC_NAME ints
#define VEC_TYPE int
#include "../vec.h"
#define VEC_NAME pairs
#define VEC_TYPE struct pair
#include "../vec.h"
#define MAP_NAME counts
#define MAP_VALUE int
#include "../map.h"
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static int at(struct ints *v, size_t i) { int *_Nullable p = ints_at(v, i); return p ? *p : -1; }
static int get(struct counts *m, struct str k) { int *_Nullable p = counts_get(m, k); return p ? *p : -1; }
int main(void) {
  struct ints v = {0};
  for (int i = 0; i < 1000; i++)
    CHECK(ints_add(&v, i * 3));
  CHECK(v.len == 1000 && at(&v, 999) == 2997 && at(&v, 1000) == -1);
  int last = 0;
  CHECK(ints_pop(&v, &last) && last == 2997 && v.len == 999);
  int *_Nullable first = ints_at(&v, 0);
  if (first)
    *first = 8;
  CHECK(at(&v, 0) == 8);
  ints_free(&v);
  CHECK(!ints_pop(&v, &last) && at(&v, 0) == -1 && ints_add(&v, 1));
  ints_free(&v);
  struct pairs ps = {0};
  struct pair one = {1, 2.5};
  CHECK(pairs_add(&ps, one));
  pairs_free(&ps);

  struct counts m = {0};
  CHECK(get(&m, STR("a")) == -1 && !counts_remove(&m, STR("a")));
  char key[2] = {0, 0};
  for (int i = 0; i < 5000; i++) {
    key[0] = (char)(i % 50);
    key[1] = (char)(i / 50);
    struct str k = {key, 2};
    CHECK(counts_put(&m, k, i));
  }
  CHECK(m.len == 5000);
  for (int i = 0; i < 5000; i += 2) {
    key[0] = (char)(i % 50);
    key[1] = (char)(i / 50);
    struct str k = {key, 2};
    CHECK(get(&m, k) == i && counts_remove(&m, k) && get(&m, k) == -1);
  }
  CHECK(m.len == 2500);
  key[0] = 1;
  key[1] = 0;
  struct str odd = {key, 2};
  CHECK(get(&m, odd) == 1 && counts_put(&m, odd, 9) && get(&m, odd) == 9 && m.len == 2500);
  CHECK(counts_put(&m, STR(""), 4) && get(&m, STR("")) == 4);
  for (int i = 0; i < 20000; i++)
    CHECK(counts_put(&m, STR("same"), i) && counts_remove(&m, STR("same")));
  counts_free(&m);
  CHECK(get(&m, odd) == -1);
  puts("ok");
  return 0;
}
