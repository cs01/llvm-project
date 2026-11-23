#include <string.h>
struct H { int *p; };
int main(int argc, char **argv) {
  int x = 0; struct H k = { &x };
  long raw[1] = { 0x4242424242 };
  memmove(&k, raw, sizeof k);
  *k.p = 1;
  return 0;
}
