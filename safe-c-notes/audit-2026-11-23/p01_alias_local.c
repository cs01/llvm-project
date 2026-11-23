#include <stdlib.h>
#include <stdio.h>
struct N { int v; struct N *_Nullable next; };
int main(void) {
  struct N *_Nullable a = malloc(sizeof(struct N));
  if (!a) return 1;
  struct N *_Nullable b = malloc(sizeof(struct N));
  if (!b) return 1;
  a->next = b; b->v = 7;
  free(b);
  long *_Nullable spray = malloc(sizeof(struct N));
  if (!spray) return 1;
  *spray = 0x41414141;
  struct N *_Nullable n = a->next;
  if (!n) return 1;
  printf("%x\n", n->v);
  return 0;
}
