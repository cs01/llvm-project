// Found by the Lean model of _Drop (rule dropHoldsOwner, 2026-11-24): a
// _Drop_free local is freed when its scope ends, so it may only hold memory
// it owns. Storing the address of a local, or a borrowed parameter, made the
// cleanup free memory it did not own. The control run turns Safe C off
// (safec.h's annotations expand to nothing).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
void keep(int *_Owned _Nullable p);

void assign_local(void) {
  int a = 0;
  int *_Nullable p _Drop_free = malloc(4);
  p = &a; // expected-error {{'p' is freed automatically when its scope ends, so it may only hold memory it owns; store an allocation, '_Take(...)' of an owner, or null, not a pointer borrowed from a local or a parameter}}
}

void init_local(void) {
  int a = 0;
  int *_Nullable p _Drop_free = &a; // expected-error {{'p' is freed automatically}}
}

void init_param(int *q) {
  int *_Nullable p _Drop_free = q; // expected-error {{'p' is freed automatically}}
}

void fine(void) {
  int *_Nullable p _Drop_free = malloc(4);
  int *_Nullable none _Drop_free = 0;
  keep(_Take(p));
  p = malloc(8);
  (void)none;
}
