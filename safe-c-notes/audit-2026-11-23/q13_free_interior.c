#include <stdlib.h>
#include <stdio.h>
struct P { int a; int b; };
int main(void) {
  struct P *_Nullable p = malloc(sizeof *p); if (!p) return 1;
  free(&p->b);
  int g = 0; 
  static int st;
  free(&st);
  puts("freed interior + static");
  return g;
}
