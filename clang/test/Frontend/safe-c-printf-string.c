// Under -fsafe-c a bounded buffer printed with %s is scanned for its
// terminator within its bounds, and the call traps if there is none
// (audit probes r08_printf, q08_printf: printf("%s") read past a char[4]).
// RUN: %clang -fsafe-c -O1 %s -o %t && %t | FileCheck %s --check-prefix=OK
// RUN: %clang -fsafe-c -O1 %s -o %t && not --crash %t bad
// RUN: %clang -O1 %s -o %t.plain && %t.plain | FileCheck %s --check-prefix=OK
#include <stdio.h>
#include <string.h>

struct R { char name[4]; char secret[16]; };

int main(int argc, char **argv) {
  struct R r;
  memcpy(r.secret, "SECRET", 7);
  (void)argv;
  if (argc == 1)
    memcpy(r.name, "abc", 4);  // terminated within bounds
  else
    memcpy(r.name, "abcd", 4); // no terminator in name
  printf("%s|%.2s|%s\n", r.name, r.name, "lit");
  return 0;
}
// OK: abc|ab|lit
