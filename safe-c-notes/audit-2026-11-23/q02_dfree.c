#include <stdlib.h>
#include <stdio.h>
struct B { int *_Nullable p; };
static void drop(struct B *b) { free(b->p); }
int main(void) {
  struct B a;
  a.p = malloc(sizeof(int));
  struct B c = a;
  drop(&a);
  int *_Nullable q = malloc(sizeof(int));
  if (!q) return 1;
  *q = 1;
  drop(&c);            /* frees q's block */
  int *_Nullable r = malloc(sizeof(int));
  if (!r) return 1;
  *r = 0x46464646;
  printf("%x (q==r: %d)\n", *q, q == r);
  return 0;
}
