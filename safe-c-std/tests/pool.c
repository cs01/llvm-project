/* A graph by handles: edges that go stale are found out, not followed. */
#include "../pool.h"
#include <stdio.h>
struct node { int v; uint32_t to_slot, to_gen; };
POOL(node, struct node);
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int test(node_pool *p) {
  node_id a, b, c;
  CHECK(node_add(p, &a) && node_add(p, &b));
  struct node *_Nullable n = node_get(p, a);
  CHECK(n != NULL);
  if (!n) return 1;
  n->v = 1; n->to_slot = b.slot; n->to_gen = b.gen;
  node_id edge = {n->to_slot, n->to_gen};
  CHECK(node_get(p, edge) != NULL);
  CHECK(node_remove(p, b));
  CHECK(node_get(p, edge) == NULL);
  CHECK(!node_remove(p, b));
  CHECK(node_add(p, &c));
  CHECK(c.slot == b.slot && c.gen != b.gen);
  CHECK(node_get(p, edge) == NULL);
  n = node_get(p, c);
  CHECK(n != NULL && n->v == 0);
  for (int i = 0; i < 1000; i++) {
    node_id x;
    CHECK(node_add(p, &x));
    if (i % 3 == 0) CHECK(node_remove(p, x));
  }
  n = node_get(p, a);
  CHECK(n != NULL && n->v == 1);
  node_id none = {0, 0}, far = {1u << 30, 1};
  CHECK(node_get(p, none) == NULL && node_get(p, far) == NULL);
  return 0;
}
int main(void) {
  node_pool p = {0};
  int r = test(&p);
  node_free(&p);
  node_id a = {0, 1};
  if (node_get(&p, a) != NULL) return 1;
  return r;
}
