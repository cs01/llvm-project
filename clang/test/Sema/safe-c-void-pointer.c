// Under -fsafe-c, turning `void *` back into a typed pointer is an error
// outside __unsafe: nothing checks that the memory has that type. Null
// constants and fresh allocations are allowed.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s
// RUN: %clang_cc1 -fsyntax-only -verify=plain %s
// plain-no-diagnostics

#include <safec.h>
typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void *calloc(size_t, size_t);
void *_Nullable get_ctx(void);
void take_ctx(void *ctx);

struct S { int x; };

void implicit(void *ctx) {
  struct S *s = ctx; // expected-error {{converting 'void *' to 'struct S *' can treat memory as the wrong type}} expected-note {{keep the typed pointer instead of 'void *'}}
  (void)s;
}

void explicit_cast(void *ctx) {
  int **pp = (int **)ctx; // expected-error {{converting 'void *' to 'int **' can treat memory as the wrong type}} expected-note {{keep the typed pointer instead of 'void *'}}
  (void)pp;
}

void allowed(struct S *s) {
  struct S *a = malloc(sizeof *a);     // fresh memory
  struct S *b = calloc(1, sizeof *b);  // fresh memory
  struct S *_Nullable c = (void *)0;   // null
  take_ctx(s);                         // T * to void * is fine
  (void)a; (void)b; (void)c;
}

void escape(void *ctx) {
  __unsafe {
    struct S *s = ctx; // the programmer vouches for the type here
    (void)s;
  }
}

// A byte view claims no type for the memory: void * to char * is allowed.
void bytes(void *buf) {
  char *b = buf;
  unsigned char *u = (unsigned char *)buf;
  (void)b; (void)u;
}
