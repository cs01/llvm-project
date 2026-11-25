#include <stdio.h>
#include <string.h>
int main(void) {
  unsigned _BitInt(1) x;
  memset(&x, 200, 1);
  int t[2] = {5, 6};
  printf("%d %d\n", (int)x, t[x]);
  return 0;
}
