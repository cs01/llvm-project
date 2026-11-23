#include <string.h>
#include <stdio.h>
struct H { int *p; };
int main(int argc, char **argv) {
  int x = 0; struct H h = { &x }; struct H k = { &x };
  memset(&h, 0x40 + argc, sizeof h);
  long raw[1] = { 0x4242424242 };
  memmove(&k, raw, sizeof k);
  bcopy(raw, &k, sizeof k);
  FILE *_Nullable f = fopen("/dev/urandom", "r");
  if (f) fread(&k, sizeof k, 1, f);
  read(0, &k, sizeof k);
  *h.p = 1;
  return 0;
}
