#include <stdio.h>
#include <string.h>
int main(void) {
  char secret[16] = "SECRETSECRET";
  char buf[4];
  memcpy(buf, "abcd", 4);
  printf("%s\n", buf);
  char out[64];
  snprintf(out, sizeof out, "%s", buf);
  puts(out);
  return (int)secret[0] * 0;
}
