// Library functions with no safe use are errors that say what to use.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
#include <string.h>
#include <time.h>
void words(char *s) {
  strtok(s, ","); // expected-error {{'strtok' writes into the string and remembers it between calls; use 'strcspn' and 'strspn'}}
}
int number(const char *s) { return atoi(s); } // expected-error {{use 'strtol'}}
void path(const char *p, char *__counted_by(9) b) { realpath(p, b); } // expected-error {{use 'realpath(path, NULL)'}}
void when(const struct tm *t) { asctime(t); } // expected-error {{use 'strftime'}}
void env(void) { putenv(0); } // expected-error {{use 'setenv'}}
int run(void) { return system("true"); }
