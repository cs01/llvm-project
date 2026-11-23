#include <stdio.h>
#include <string.h>
struct H { int *p; };
int main(void) {
  int x = 1;
  struct H h = { &x };
  void *v = &h;
  char *c = v;
  unsigned char *u = (unsigned char *)&h;
  c[0] = 0x41; c[1] = 0x41; c[2] = 0x41; c[3] = 0x41; c[4] = 0; c[5] = 0; c[6] = 0; c[7] = 0;
  u[0] = 0x42;
  *h.p = 7;
  return 0;
}
