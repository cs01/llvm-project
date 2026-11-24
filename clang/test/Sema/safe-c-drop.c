// _Drop runs a cleanup at the end of the scope, like Rust's Drop. A _Drop
// pointer is never given away by name (the cleanup would free it again);
// _Take(p) moves it out and leaves null for the cleanup. The control run
// pins the ownership rules off: the lifetime analysis still sees the cleanup
// as a use, so a double free through it is caught either way.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct buf { char *_Owned _Nullable __counted_by_or_null(cap) p; size_t cap; };
void buf_free(struct buf *_Excl b) { free(_Take(b->p)); }
void keep(int *_Nullable _Owned p);

void scoped(void) {
  struct buf b _Drop(buf_free) = {0, 0};
  int *_Nullable p _Drop_free = malloc(sizeof(int));
  if (p)
    *p = 1;
  (void)b;
}

void freed_by_name(void) {
  int *_Nullable p _Drop_free = malloc(sizeof(int)); // expected-error {{object allocated by 'malloc' does not live long enough}} off-error {{object allocated by 'malloc' does not live long enough}}
  free(p); // expected-error {{'p' is freed automatically at the end of its scope ('_Drop'), so giving it away here would free it twice; move it out with '_Take(p)', which leaves null for the cleanup}} expected-note {{freed here}} off-note {{freed here}}
} // expected-note {{later used by the cleanup function of 'p' at scope exit}} off-note {{later used by the cleanup function of 'p' at scope exit}}

void moved(void) {
  int *_Nullable p _Drop_free = malloc(sizeof(int)); // expected-error {{object allocated by 'malloc' does not live long enough}} off-error {{object allocated by 'malloc' does not live long enough}}
  keep(p); // expected-error {{'p' is freed automatically}} expected-note {{freed here}} off-note {{freed here}}
  int *_Nullable q _Drop_free = malloc(sizeof(int));
  keep(_Take(q));
} // expected-note {{later used by the cleanup function of 'p' at scope exit}} off-note {{later used by the cleanup function of 'p' at scope exit}}

_Returns_owned int *_Nullable returned(void) {
  int *_Nullable p _Drop_free = malloc(sizeof(int));
  return p; // expected-error {{'p' is freed automatically}}
}
_Returns_owned int *_Nullable returned_ok(void) {
  int *_Nullable p _Drop_free = malloc(sizeof(int));
  return _Take(p);
}
