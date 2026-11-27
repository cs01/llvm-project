// C leaves NULL to a library function undefined unless the function allows it.
// Apple's <time.h> marks none of these nonnull, so ctime(0) compiled and
// crashed inside libc (found by the corpus audit: spatial/p/w9/i084.c).
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <time.h>
char *__null_terminated c(const time_t *_Nullable p) {
  return ctime(p); // expected-error {{passing nullable pointer to nonnull parameter 1}} expected-note {{add a null check before the call}}
}
struct tm *l(const time_t *_Nullable p) {
  return localtime(p); // expected-error {{passing nullable pointer to nonnull parameter 1}} expected-note {{add a null check before the call}}
}
struct tm *g(const time_t *_Nullable p) {
  return gmtime(p); // expected-error {{passing nullable pointer to nonnull parameter 1}} expected-note {{add a null check before the call}}
}
time_t m(struct tm *_Nullable q) {
  return mktime(q); // expected-error {{passing nullable pointer to nonnull parameter 1}} expected-note {{add a null check before the call}}
}
int good(void) {
  time_t t = time(0); // time() allows NULL
  struct tm *tm = localtime(&t);
  return tm ? tm->tm_year : 0;
}
