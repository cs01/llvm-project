// Under -fsafe-c an integer cast to a pointer is a forged pointer and is an
// error, except null, 'void *' (not dereferenceable; leaving it needs a
// checked conversion) and system-header macros.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __UINTPTR_TYPE__ uintptr_t;

int deref(long x) { return *(int *)x; } // expected-error {{converting 'long' to 'int *' makes up an address}}
int subscript(long x) { return ((int *)x)[3]; } // expected-error {{makes up an address}}
int roundtrip(int *q) { return *(int *)((uintptr_t)q + 8); } // expected-error {{makes up an address}}

// Comparing against a sentinel address, as with MAP_FAILED.
int is_failed(void *_Nullable p) { return p == (void *)-1; }
uintptr_t address(int *q) { return (uintptr_t)q; }
int *_Nullable none(void) { return (int *)0; }
