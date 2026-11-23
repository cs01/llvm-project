#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
struct H { int *p; };
int main(int argc, char **argv) {
  int x = 0; struct H h = { &x };
  arc4random_buf(&h, sizeof h);
  *h.p = 1;
  return 0;
}
