// Under -fsafe-c a buffer passed where a terminated string is expected is
// checked for its terminator for the duration of the call; a callee that
// could keep the pointer would see the terminator erased later (audit
// finding 4 / the string hole), so the parameter must be noescape.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

static char *_Nullable __null_terminated keep; // expected-error {{holds a pointer nothing tracks}}
void retain(char *__null_terminated s) { keep = s; }
int length(const char *__null_terminated s __attribute__((noescape)));

void f(void) {
  char buf[8] = "abc";
  retain(buf); // expected-error {{'s' may keep the pointer, and a later write to the buffer could erase the terminator; mark 's' 'noescape'}}
  (void)length(buf);
}
