// Under -fnullability-default=nonnull an unannotated pointer return type or
// struct field means _Nonnull, as unannotated parameters already do.
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nonnull -verify %s
// Under the nullable default the same code is quiet.
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nullable -verify=nullable %s
// nullable-no-diagnostics

#define NULL ((void *)0)

int *ret_null_local(void) {
  int *q = NULL;
  return q; // expected-warning {{returning nullable pointer from function with nonnull return type}} expected-note {{add a null check before returning}}
}

int *_Nullable ret_null_annotated(void) {
  int *q = NULL;
  return q;
}

int *ret_checked(int *_Nullable p, int *fallback) {
  if (p)
    return p;
  return fallback;
}

struct S {
  int *f;
  int *_Nullable g;
};

void store_null_field(struct S *_Nonnull s) {
  s->f = NULL; // expected-warning {{assigning nullable pointer to nonnull member 'f'}} expected-note {{add a null check before assigning}}
}

void store_null_nullable_field(struct S *_Nonnull s) { s->g = NULL; }

void store_checked(struct S *_Nonnull s, int *_Nullable p) {
  if (p)
    s->f = p;
}
