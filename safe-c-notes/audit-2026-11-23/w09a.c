#include <string.h>
struct H { int *p; };
int main(int argc, char **argv) {
  int x = 0; struct H h = { &x };
  memset(&h, 0x40 + argc, sizeof h);
  *h.p = 1;
  return 0;
}
