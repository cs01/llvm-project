// Under -fsafe-c: va_arg needs __unsafe (nothing checks the count or types),
// scanf's %p is rejected (it makes up a pointer from text), and main's argv
// holds terminated strings, so argv[1] can go to strcmp.
// RUN: %clang_cc1 -mllvm -safe-c-redeclared=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-redeclared=false -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __builtin_va_list va_list;
int sscanf(const char *, const char *, ...) __attribute__((format(scanf, 2, 3)));
int strcmp(const char *, const char *);

int sum(int n, ...) {
  va_list ap;
  __builtin_va_start(ap, n);
  int s = __builtin_va_arg(ap, int); // expected-error {{'va_arg' can switch off Safe C's checks}}
  int t = 0;
  __unsafe { t = __builtin_va_arg(ap, int); }
  __builtin_va_end(ap);
  return s + t;
}

void *_Nullable parse(const char *__null_terminated s) {
  void *_Nullable p = 0;
  sscanf(s, "%p", &p); // expected-error {{scanf writes whatever}} expected-error {{scanf's '%p', which makes up a pointer, can switch off Safe C's checks}}
  return p;
}

int main(int argc, char **argv) {
  return argc > 1 && strcmp(argv[1], "x") == 0;
}
