// One block, two owners, through the back doors: a pointer stored in a local
// struct's field keeps its loans for later reads of that field, so freeing
// the block through a second owner is a double free; and memcpy of a struct
// with an owned field is a copy of the owner. The control run pins the
// ownership rules off.
// off-no-diagnostics
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety \
// RUN:   -fnullability-default=nonnull -fnullability-safety -include safec.h -verify=off %s

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
void *memcpy(void *__sized_by(n) d, const void *__sized_by(n) s, size_t n);
struct B { int *_Owned _Nullable p; };

void two_stores(void) {
  int *_Nullable o = malloc(4); // expected-error {{object allocated by 'malloc' does not live long enough}}
  struct B a = {0}, c = {0};
  a.p = o;
  c.p = o; // expected-note {{aliases}}
  free(_Take(a.p)); // expected-note {{freed here}}
  free(_Take(c.p)); // expected-note {{later used here}}
}

void two_inits(void) {
  int *_Nullable o = malloc(4); // expected-error {{object allocated by 'malloc' does not live long enough}}
  struct B a = {o};
  struct B c = {o}; // expected-note {{aliases}}
  free(_Take(a.p)); // expected-note {{freed here}}
  free(_Take(c.p)); // expected-note {{later used here}}
}

void byte_copy(void) {
  struct B a = {0}, c = {0};
  a.p = malloc(sizeof(int));
  memcpy(&c, &a, sizeof a); // expected-error {{copying 'struct B' byte by byte duplicates its owned field 'p', and both copies would free it; move the field with '_Take' and assign the others, or pass a pointer to the struct instead}}
  free(_Take(a.p));
  free(_Take(c.p));
}

void fine(void) {
  int *_Nullable o = malloc(4);
  struct B a = {0};
  a.p = o;
  free(_Take(a.p));
}
