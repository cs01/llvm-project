#include <stdlib.h>
#include <stdio.h>
struct B { int *_Nullable p; };
static void drop(struct B *b) { free(b->p); }
int main(void) {
  struct B a; 
  a.p = malloc(sizeof(int));
  struct B c = a;
  drop(&a);
  drop(&c);
  puts("double free done");
  return 0;
}
