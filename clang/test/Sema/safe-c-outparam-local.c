// A local pointer carries bounds by default, so `&local` cannot go where a
// callee stores a pointer without them (strtol's end pointer, a thread
// handle). The note says which declaration fixes it; with that declaration
// the call compiles. The control run is plain C.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics
#include <ptrcheck.h>

long to_long(const char *s, char *__null_terminated *_Nullable end); // #to_long
typedef struct handle_ *handle;
void open_handle(handle *out); // #open_handle

long bad_end(const char *s) {
  char *end; // expected-note {{the callee stores a pointer without bounds into 'end', but a local pointer carries them by default; declare it as 'char *__null_terminated end;'}}
  return to_long(s, &end); // expected-error {{that adds '__terminated_by' attribute}} expected-note@#to_long {{passing argument to parameter 'end' here}}
}

void bad_handle(void) {
  handle h; // expected-note {{declare it as 'handle __single h;'}} expected-note {{'h' declared here}}
  open_handle(&h); // expected-error {{incompatible}} expected-note@#open_handle {{passing argument to parameter 'out' here}}
}

long good(const char *s) {
  char *__null_terminated end;
  handle __single h;
  open_handle(&h);
  return to_long(s, &end);
}
