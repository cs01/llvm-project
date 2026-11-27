// An _Atomic local pointer marked _Static is __bidi_indexable (bounds-safety rejects
// the unmarked one itself): pointer plus bounds,
// wider than an atomic word, so a racing read could pair one store's address
// with another's bounds. (It also crashed the ownership check.) A __single
// atomic pointer is one word and is fine.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
int g;

int wide(void) {
  _Atomic(int *_Static) p = 0; // expected-error-re {{'_Atomic(int *__bidi_indexable{{.*}})' is a pointer with bounds, wider than one atomic word: a read could see the address from one store and the bounds from another; make it '__single'}}
  return p != 0;
}

int single(void) {
  _Atomic(int *__single) p = &g;
  int *q = p;
  return *q;
}
