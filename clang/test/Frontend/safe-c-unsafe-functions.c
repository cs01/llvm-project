// Library functions with no safe use are errors that say what to use.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
void words(char *s) {
  strtok(s, ","); // expected-error {{'strtok' writes into the string and remembers it between calls; use 'str_before' and 'str_after' (text.h)}}
}
int number(const char *s) { return atoi(s); } // expected-error {{use 'strtol'}}
void path(const char *p, char *__counted_by(9) b) { realpath(p, b); } // expected-error {{use 'realpath(path, NULL)'}}
void when(const struct tm *t) { asctime(t); } // expected-error {{use 'strftime'}}
void env(void) { putenv(0); } // expected-error {{use a small function marked '__unsafe' that calls 'setenv', before any 'getenv'}}
int run(void) { return system("true"); }
void set(void) { setenv("A", "b", 1); } // expected-error {{'setenv' may free what 'getenv' returned}}
// What the errors say to use.
size_t when2(const struct tm *t) { char b[64]; return strftime(b, sizeof b, "%Y", t); }
int now(time_t t) { struct tm m; return localtime_r(&t, &m) != NULL; }
int where(void) { char b[256]; return getcwd(b, sizeof b) != NULL; }
void whole(const char *p) { char *_Owned _Nullable __null_terminated r = realpath(p, NULL); free(r); }
int temp(void) { char b[] = "/tmp/aXXXXXX"; return mkstemp(b); }
int who(void) { char b[64]; return getlogin_r(b, sizeof b) + ttyname_r(0, b, sizeof b); }
int same(const char *a, const char *b) { return strcoll(a, b); }
long num(const char *s) { return strtol(s, NULL, 10); }
