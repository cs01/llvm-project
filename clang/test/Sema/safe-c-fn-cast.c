// Casting a function to a typedef of its own type (found porting inih,
// 2026-11-24): the two differ only in an implicit __single, and a plain
// assignment was accepted, but the cast was an error.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
typedef char *(*reader)(char *, int);
char *rd(char *s, int n);
char *_Nullable rd_null(char *s, int n);
void ok(void) {
  reader a = rd, b = (reader)rd;
  (void)a; (void)b;
}
typedef int (*cmp)(const void *, const void *);
int icmp(const int *a, const int *b);
void bad(void) {
  cmp c = (cmp)icmp; // expected-error {{can treat memory as the wrong type}} expected-note {{a pointer cast is allowed}}
  void (*f)(int *) = (void (*)(int *))rd; // expected-error {{can treat memory as the wrong type}} expected-note {{a pointer cast is allowed}}
  (void)c; (void)f;
}
