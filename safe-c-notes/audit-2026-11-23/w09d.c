#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
struct H { int *p; };
int main(int argc, char **argv) {
  int x = 0; struct H k = { &x };
  int *arr[2] = { &x, &x };
  long raw[2] = { 0x4242424242, 0x4343434343 };
  memset_pattern8(&k, raw, sizeof k);
  swab(raw, arr, sizeof arr);
  recv(0, &k, sizeof k, 0);
  pread(0, &k, sizeof k, 0);
  getentropy(&k, sizeof k);
  fgets((char *)arr, 8, stdin);
  strlcpy((char *)&k, "AAAAAAA", 8);
  snprintf((char *)&k, 8, "AAAAAAA");
  *k.p = 1;
  return 0;
}
