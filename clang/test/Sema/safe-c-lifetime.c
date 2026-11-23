// Under -fsafe-c, Clang's lifetime safety analysis always runs and its
// findings (use after free or scope, returned stack addresses) are errors
// that no -Wno- flag or pragma can turn off. Calls to functions marked
// ownership_takes free their argument.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -Wno-lifetime-safety \
// RUN:   -fno-lifetime-safety-c -lifetime-safety-max-cfg-blocks=1 -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -fno-builtin -verify %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14.0.0 -fsyntax-only -fsafe-c \
// RUN:   -fbounds-safety -fnullability-safety -fnullability-default=nonnull \
// RUN:   -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t) __attribute__((alloc_size(1)));
void free(void *_Nullable);
void sink(int *_Nullable p) __attribute__((ownership_takes(malloc, 1)));

int uaf(void) {
  int *_Nullable p = malloc(sizeof(int)); // expected-error {{object allocated by 'malloc' does not live long enough}}
  int *_Nullable q = p;                   // expected-note {{aliases the storage}}
  free(p);                                // expected-note {{freed here}}
  return q ? *q : 0;                      // expected-note {{later used here}}
}

#pragma clang diagnostic ignored "-Wlifetime-safety-use-after-free"
int callee_frees(void) {
  int *_Nullable p = malloc(sizeof(int)); // expected-error {{does not live long enough}}
  sink(p);                                // expected-note {{freed here}}
  return p ? *p : 0;                      // expected-note {{later used here}}
}

int *escape(void) {
  int x = 1;
  int *p = &x; // expected-error {{stack memory associated with local variable 'x' is returned}}
  return p;    // expected-note {{returned here}}
}
