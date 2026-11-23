#include <stdio.h>
__attribute__((noinline)) static int big(int i) { char buf[1 << 22]; buf[i] = 1; return buf[i + 1]; }
int main(int argc, char **argv) { return big(argc); }
