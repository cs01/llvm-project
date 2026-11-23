#include <stdio.h>
int *_Nullable g;
static void set(void) { int x = 42; g = &x; }
static void clobber(void) { volatile int a[16]; for (int i = 0; i < 16; i++) a[i] = 0x43434343; }
int main(void) {
  set(); clobber();
  if (g) printf("%x\n", *g);
  return 0;
}
