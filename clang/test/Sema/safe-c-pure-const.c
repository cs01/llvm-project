// Under -fsafe-c, a function marked pure or const must keep its promise:
// CodeGen marks every call memory(read) or memory(none), so a body that
// writes memory (or, for const, reads it) lets the optimizer merge or drop
// calls. The body is checked; system-header declarations stay trusted.
// RUN: %clang_cc1 -mllvm -safe-c-redeclared=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-redeclared=false -fsyntax-only -include safec.h -verify=plain %s

__SIZE_TYPE__ strlen(const char *);
int counter;
const int limit = 10;
volatile int reg;
void log_it(int);
__attribute__((pure)) int peek(const int *p);
__attribute__((const)) int square(int x);
__attribute__((noreturn)) void die(void);

__attribute__((pure)) int ok_pure(const int *__counted_by(n) p, int n, const char *s) {
  int sum = 0;
  int local[4] = {0};
  struct { int a; } st;
  st.a = 1;
  local[1] = st.a;
  for (int i = 0; i < n; ++i)
    sum += p[i];
  if (sum < 0)
    die();
  return sum + counter + peek(p) + square(n) + (int)strlen(s) + local[1];
}

__attribute__((pure)) int bad_pure(int *p) {
  *p = 1;           // expected-error {{'pure' function 'bad_pure' writes memory other than its own local variables}}
  counter++;        // expected-error {{writes memory other than its own local variables}}
  log_it(*p);       // expected-error {{calls 'log_it', which is not marked 'pure' or 'const'}}
  int (*fp)(int) = square;
  (void)reg;        // expected-error {{accesses volatile memory}}
  return fp(2);     // expected-error {{calls through a function pointer}}
}

__attribute__((const)) int ok_const(int x) {
  int a[3] = {x, x, x};
  return a[1] * limit + square(x);
}

__attribute__((const)) int bad_const(const int *p) {
  return *p        // expected-error {{'const' function 'bad_const' reads memory through a pointer}}
         + counter // expected-error {{reads the global variable 'counter', which can change}}
         + peek(p); // expected-error {{calls 'peek', which is not marked 'const'}}
}

// pure added after the definition would go unchecked, but clang already drops
// it, so the earlier calls and later ones stay ordinary calls.
static int f(int *p) { *p = 2; return 0; } // expected-note {{previous definition is here}} plain-note {{previous definition is here}}
__attribute__((pure)) int f(int *p); // expected-warning {{attribute declaration must precede definition}} plain-warning {{attribute declaration must precede definition}}

__attribute__((pure)) int escape(int *p) {
  __unsafe { *p = 3; }
  return 0;
}
