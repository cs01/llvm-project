#include <stdio.h>
struct S { int *p; };
static struct S mk(void) { int x = 1; struct S s = { &x }; return s; }
static void outp(int *_Nullable *out) { int y = 2; *out = &y; }
static int clobber(int d) { volatile int a[64]; for (int i = 0; i < 64; i++) a[i] = 0x45454545; return d ? clobber(d-1) + a[3] : 0; }
int main(void) {
  struct S s = mk();
  int *_Nullable o = 0; outp(&o);
  clobber(8);
  printf("%x\n", *s.p);
  if (o) printf("%x\n", *o);
  return 0;
}
