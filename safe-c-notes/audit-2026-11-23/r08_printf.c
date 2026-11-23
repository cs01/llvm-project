#include <stdio.h>
#include <string.h>
struct R { char name[4]; char secret[16]; };
int main(void) {
  struct R r;
  memcpy(r.name, "abcd", 4);
  memcpy(r.secret, "SECRETSECRET", 13);
  printf("%s\n", r.name);
  return 0;
}
