#include <stdbool.h>
#include <stdio.h>
#include <string.h>
enum e : bool { A };
int main(void) { /* read at -O2: prints t[200]; no trap. O0 masks */
  enum e x = A;
  memset(&x, 200, 1);
  int t[2] = {5, 6};
  printf("%d %d\n", (int)x, t[x]);
  return 0;
}
