#include <stdio.h>
#include <stdbool.h>
int secret[1] = {0x48484848};
static int pick(bool *b) { int t[2] = {1, 2}; return t[*b]; }
int main(int argc, char **argv) {
  int raw = 200 + argc;
  int a[2] = {10, 20};
  bool *b = (bool *)&raw;
  printf("%x %d\n", pick(b), a[*b]);
  return 0;
}
