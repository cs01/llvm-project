// A count naming a member of an anonymous struct or union is an error; the
// zero-init check that followed it crashed on the anonymous member.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fbounds-safety -include safec.h -fsyntax-only -Xclang -verify %s
struct s {
  struct { int n; };
  int *__counted_by(n) p; // expected-error {{count expression on struct field may only reference other fields of the same struct}}
};
int f(void) {
  struct s s = {0};
  return 0;
}

// Assigning through the struct afterwards reached a third check (the count
// analysis runs after the error); it is skipped once there is an error.
struct t {
  struct { int n; };
  int *__counted_by(n) p; // expected-error {{count expression on struct field may only reference other fields of the same struct}}
};
int g(int *a) {
  struct t t = {0};
  t.p = a;
  t.n = 4;
  return 0;
}
