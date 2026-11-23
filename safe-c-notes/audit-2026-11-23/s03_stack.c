#include <stdio.h>
struct S { int *_Nullable p; };
__attribute__((noinline)) static struct S mk(void) { int x[64] = {0}; x[32] = 1; struct S s = { &x[32] }; return s; }
__attribute__((noinline)) static void fill(struct S *s) { int y[64] = {0}; y[32] = 2; s->p = &y[32]; }
__attribute__((noinline)) static void clobber(void) { volatile int a[256]; for (int i = 0; i < 256; i++) a[i] = 0x45454545; }
int main(void) {
  struct S s = mk();
  struct S t = {0};
  fill(&t);
  clobber();
  int *_Nullable a = s.p, *_Nullable b = t.p;
  if (a && b) printf("%x %x\n", *a, *b);
  return 0;
}
