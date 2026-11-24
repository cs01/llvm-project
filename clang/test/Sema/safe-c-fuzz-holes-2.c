// Fuzzing holes 09, 16 and 18 (2026-11-23):
//  09 an indirect call through a pointer whose parameter is _Exclusive ends the
//     caller's borrows, as a direct call does
//  16 `*&q = v` is an assignment to q (the loan is kept)
//  18 a copy of a _Drop_free pointer dies when the variable's cleanup frees
//     it; moving the pointer out with _Take keeps the memory
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify=off %s

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct S { int *_Owned _Nullable p; };
void sfree(struct S *_Exclusive s) { free(_Take(s->p)); }

int h09(void) {
  void (*fn)(struct S *_Exclusive) = sfree;
  struct S s = {0};
  s.p = malloc(4);
  int *_Nullable q = s.p; // expected-error {{this pointer is borrowed from 's.p'}}
  fn(&s);                 // expected-note {{the borrow ends here}}
  return q ? *q : 0;      // expected-note {{later used here}}
}

int h16(void) {
  struct S s = {0};
  s.p = malloc(4);
  int *_Nullable q = 0;
  *&q = s.p;              // expected-error {{this pointer is borrowed from 's.p'}}
  free(_Take(s.p));       // expected-note {{the borrow ends here}}
  return q ? *q : 0;      // expected-note {{later used here}}
}

int h18(void) {
  int *_Nullable keep = 0;
  {
    int *_Nullable p _Drop_free = malloc(4); // expected-note {{allocated into a '_Drop' variable here, which frees it at the end of its scope}} off-note {{allocated into a '_Drop' variable here}}
    keep = p;
  }
  return keep ? *keep : 0; // expected-error {{this memory was freed when the '_Drop' variable holding it went out of scope; keep the variable alive as long as the copy, or move the pointer out of it with '_Take' to keep the memory}} off-error {{this memory was freed when the '_Drop' variable holding it went out of scope}}
}

int h18_take(void) {
  int *_Nullable keep = 0;
  {
    int *_Nullable p _Drop_free = malloc(4);
    keep = _Take(p);
  }
  int v = keep ? *keep : 0;
  free(keep);
  return v;
}
