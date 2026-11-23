#include <sys/cdefs.h>
#include <stdio.h>
int main(int argc, char **argv) {
  long addr = 0x41414141 + argc;
  int *p = __CAST_AWAY_QUALIFIER(addr, const, int *);
  *p = 1;
  return 0;
}
