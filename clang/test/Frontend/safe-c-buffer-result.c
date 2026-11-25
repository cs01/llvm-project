// What fgets or getcwd returns is the buffer it was given: it is tested or
// ignored, and never kept as a string, however the result gets there.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdio.h>
#include <string.h>
#include <unistd.h>
struct _View v { const char *__null_terminated _Nullable s; };
void kept(int c, char *__counted_by(8) b) {
  const char *__null_terminated _Nullable p = getcwd(b, 8); // expected-error {{a string inside a buffer that can still be changed}}
  p = fgets(b, 8, stdin); // expected-error {{a string inside a buffer}}
  p = c ? "a" : getcwd(b, 8); // expected-error {{a string inside a buffer}}
  p = fgets(b, 8, stdin) ?: "a"; // expected-error {{a string inside a buffer}}
  struct v x = {fgets(b, 8, stdin)}; // expected-error {{a string inside a buffer}}
  const char *__null_terminated _Nullable a[1] = {getcwd(b, 8)}; // expected-error {{a string inside a buffer}}
}
int tested(void) {
  char b[300] = {0};
  if (getcwd(b, 300) == NULL) return 1;
  if (!fgets(b, 9, stdin)) return 0;
  (void)fgets(b, 9, stdin);
  fgets(b, 9, stdin);
  while (fgets(b, 9, stdin) != NULL) {}
  for (; fgets(b, 9, stdin);) {}
  return fgets(b, 9, stdin) && b[0];
}
