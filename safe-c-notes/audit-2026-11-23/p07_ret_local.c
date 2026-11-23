#include <stdio.h>
struct S { int *p; };
static struct S mk(void) { int x = 1; struct S s = { &x }; return s; }
static void clobber(void) { volatile int a[32]; for (int i = 0; i < 32; i++) a[i] = 0x45454545; }
int main(void) {
  struct S s = mk(); clobber();
  printf("%x\n", *s.p);
  return 0;
}
