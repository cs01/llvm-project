#include <string.h>
#include <stdio.h>
struct H { int *p; };
int target = 1;
int main(void) {
  int x = 0; struct H k = { &x };
  long raw[1] = { (long)&target };
  memcpy(&k, raw, sizeof k);     /* long bytes become an int* */
  *k.p = 0x60606060;
  printf("target=%x\n", target);
  return 0;
}
