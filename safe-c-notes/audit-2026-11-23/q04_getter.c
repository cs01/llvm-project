#include <stdlib.h>
#include <stdio.h>
struct T { int *_Nullable item; };
static int *_Nullable get(struct T *t) { return t->item; }
static void destroy(struct T *t) { free(t->item); t->item = 0; }
int main(void) {
  struct T t; t.item = malloc(sizeof(int));
  int *_Nullable it = get(&t);
  if (!it) return 1;
  *it = 3;
  destroy(&t);
  int *_Nullable s = malloc(sizeof(int)); if (!s) return 1; *s = 0x47474747;
  printf("%x\n", *it);
  return 0;
}
