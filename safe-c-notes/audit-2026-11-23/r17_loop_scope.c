#include <stdio.h>
struct S { int *_Nullable p; };
static int clobber(int d) { volatile int a[64]; for (int i = 0; i < 64; i++) a[i] = 0x53535353; return d ? clobber(d-1) + a[3] : 0; }
static void fill(struct S *s) { int x = 9; s->p = &x; }
int main(void) {
  struct S s = {0};
  int *_Nullable arr[1] = {0};
  fill(&s);
  clobber(4);
  int *_Nullable q = s.p;
  if (q) printf("%x\n", *q);
  return 0;
}
