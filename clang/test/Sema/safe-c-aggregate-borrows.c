// Fuzzing holes 04, 05, 17 (2026-11-23): a local array of borrows (pointers
// or views) and a view nested in a view keep the loans of what they hold,
// as a view does for its fields. The control run turns the ownership rules
// off: views and arrays are tracked without it, the field take is not.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify=off %s

typedef __SIZE_TYPE__ size_t;
void *_Nullable calloc(size_t, size_t);
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct _View span { const int *__counted_by(n) p; int n; };
struct _View wrap { struct span s; };
struct S { int *_Owned _Nullable p; };

int nested(void) {
  int *_Nullable a = calloc(4, sizeof(int)); // expected-error {{object allocated by 'calloc' does not live long enough}} off-error {{object allocated by 'calloc' does not live long enough}}
  if (!a)
    return 1;
  struct wrap w = {{a, 4}}; // expected-note {{local variable 'a' aliases}} off-note {{local variable 'a' aliases}}
  free(a);                  // expected-note {{freed here}} off-note {{freed here}}
  return w.s.p[1];          // expected-note {{later used here}} off-note {{later used here}}
}

int view_array(void) {
  int *_Nullable a = calloc(4, sizeof(int)); // expected-error {{object allocated by 'calloc' does not live long enough}} off-error {{object allocated by 'calloc' does not live long enough}}
  if (!a)
    return 1;
  struct span arr[2] = {{a, 4}, {a, 4}}; // expected-note {{local variable 'a' aliases}} off-note {{local variable 'a' aliases}}
  free(a);                               // expected-note {{freed here}} off-note {{freed here}}
  return arr[1].p[1];                    // expected-note {{later used here}} off-note {{later used here}}
}

int pointer_array(void) {
  struct S s = {0};
  s.p = malloc(4);
  int *_Nullable q = 0;
  int *_Nullable r[1] = {0};
  r[0] = s.p; // expected-error {{this pointer is borrowed from 's.p'}}
  q = r[0];
  free(_Take(s.p)); // expected-note {{the borrow ends here}}
  return q ? *q : 0; // expected-note {{later used here}}
}

int fine(void) {
  int a[4] = {1, 2, 3, 4};
  struct span parts[2] = {{a, 2}, {a + 2, 2}};
  int *ptrs[2] = {&a[0], &a[1]};
  struct wrap w = {{a, 4}};
  return parts[1].p[0] + *ptrs[1] + w.s.p[3];
}
