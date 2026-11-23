#include <stdio.h>
int main(int argc, char **argv) {
  long addr = 0x41414140 + argc * 0;
  void *t = (void *)addr;
  goto *t;
  return 0;
}
