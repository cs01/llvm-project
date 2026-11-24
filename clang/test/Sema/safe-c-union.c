// Under -fsafe-c, reading a pointer-holding member of a union whose members
// have different types is an error: it can read an int's bytes as a pointer.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

union U { long i; int *p; }; // expected-error {{holds a pointer nothing tracks}}
union Same { int *a; int *b; }; // expected-error {{holds a pointer nothing tracks}} expected-error {{holds a pointer nothing tracks}}
union Bytes { int i; char c[4]; };

int pun(union U *u) {
  u->i = 0x41;
  return *u->p; // expected-error {{accessing 'p' of a union whose members have different types}}
}

int same(union Same *s) { return *s->b; }  // ok: every member has the same type
int bytes(union Bytes *b) { return b->c[0] + b->i; } // ok: no pointers

int escape(union U *u) {
  __unsafe { return *u->p; }
}
