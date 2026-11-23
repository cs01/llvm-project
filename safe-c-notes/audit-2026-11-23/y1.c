#include <string.h>
struct H { int *p; };
int main(int argc, char **argv) {
  int x = 0; struct H k = { &x }; struct H h = { &x }; struct H j = { &x };
  long raw[1] = { 0x4242424242 };
  memcpy(&k, raw, sizeof k);
  memmove(&h, raw, sizeof h);
  memset(&j, argc, sizeof j);
  return 0;
}
