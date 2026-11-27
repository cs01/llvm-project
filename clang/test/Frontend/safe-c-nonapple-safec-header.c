// On a C library that does not annotate itself (glibc, musl, Emscripten), the
// driver's safec.h redeclares strlen and friends with __null_terminated. It is
// included by full path, so it is not a system header, and Safe C rejected its
// own redeclarations: every Linux or browser compile failed with 8 errors. Only
// the resource-directory safec.h is exempt; a project's own one is not.
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang -fsafe-c --target=x86_64-unknown-linux-gnu -nostdlibinc -fsyntax-only -Xclang -verify %t/main.c
// RUN: %clang -fsafe-c --target=x86_64-unknown-linux-gnu -nostdlibinc -fsyntax-only -Xclang -verify -I %t %t/spoof.c

//--- main.c
typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void free(void *);
int main(void) {
  int *_Owned _Nullable p = malloc(4);
  if (!p)
    return 1;
  free(p);
  free(p); // expected-error {{'p' is freed after it was freed on line 8}}
  return 0;
}

//--- own/safec.h
char *strcpy(char *, const char *__null_terminated); // expected-error {{'strcpy' is a library function; include its header, whose declaration says what it accesses}}

//--- spoof.c
#include "own/safec.h"
int f(void) { return 0; }
