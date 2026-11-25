// Library functions whose header does not say what they access are not called
// unchecked (blind attack causes 6, 22, 24, 25, 2026-11-24).
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -mllvm -safe-c-ownership=false -fsyntax-only -Xclang -verify %s
// Ordinary use stays accepted.
// RUN: %clang -fsafe-c -mllvm -safe-c-ownership=false -fsyntax-only %S/Inputs/safe-c-library-use.c
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
void bad(int fd) {
  char b[4] = {0};
  send(fd, b, 5000, 0); // expected-error {{nothing says how much memory 'send' accesses}}
  struct in_addr a;
  inet_pton(AF_INET6, "1:2::3", &a); // expected-error {{nothing says how much memory 'inet_pton' accesses}}
  setvbuf(stdout, b, _IOFBF, 4);     // expected-error {{nothing says how much memory 'setvbuf' accesses}}
  setenv("A", "b", 1);               // expected-error {{nothing says how much memory 'setenv' accesses}}
}
int good(void) {
  char b[16];
  snprintf(b, sizeof b, "%d", 5);
  size_t n = strlen(b);
  puts("hi");
  FILE *_Nullable f = fopen("/dev/null", "r");
  if (f) { (void)fread(b, 1, sizeof b, f); fclose(f); }
  return (int)n + abs(-1) + (int)getpid() + (int)time(NULL);
}
