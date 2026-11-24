// Fuzzing holes 01 and 11 (2026-11-23):
//  01 a getter returning (a borrow of) an owned global gives a borrow of the
//     globals, which ends when they change; functions returning new memory
//     (_Returns_owned) are not getters
//  11 lending &g to an _Exclusive parameter ends borrows of globals, as
//     _Take(g) does
// RUN: %clang_cc1 -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
static int *_Owned _Nullable g;
static int count;
int *_Nullable get(void) { int *_Nullable r = g; return r; }
_Returns_owned int *_Nullable make(void) { count++; return malloc(4); }
void reset(int *_Owned _Nullable *_Exclusive slot) { free(_Take(*slot)); }

int getter(void) {
  int *_Nullable q = get(); // expected-error {{does not live long enough}}
  free(_Take(g));           // expected-note {{}}
  return q ? *q : 0;        // expected-note {{later used here}}
}

int lend(void) {
  int *_Nullable q = g; // expected-error {{this pointer is borrowed from 'g'}}
  reset(&g);            // expected-note {{the borrow ends here}}
  return q ? *q : 0;    // expected-note {{later used here}}
}

int fresh(void) {
  int *_Nullable p = make();
  free(_Take(g));
  int v = p ? *p : 0;
  free(p);
  return v;
}

// Fuzzer round 3: an _Exclusive local pointing at a global is a second name for
// it, and a lifetimebound getter of a global borrows the globals.
struct S2 { int *_Owned _Nullable p; };
static struct S2 G2;
struct _View V2 { int *_Nullable p; };
struct V2 view2(struct S2 *s _Lifetimebound) { struct V2 v = {s->p}; return v; }
void sfree2(struct S2 *_Exclusive s) { free(_Take(s->p)); }

int excl_alias_global(void) {
  int *_Nullable q = G2.p; // expected-error {{this pointer is borrowed from 'G2.p'}}
  struct S2 *_Exclusive x = &G2;
  free(_Take(x->p));       // expected-note {{the borrow ends here}}
  return q ? *q : 0;       // expected-note {{later used here}}
}

int getter_global(void) {
  int *_Nullable q = view2(&G2).p; // expected-error {{'view2' returns a borrow of '&G2'}}
  sfree2(&G2);                     // expected-note {{the borrow ends here}}
  return q ? *q : 0;               // expected-note {{later used here}}
}
