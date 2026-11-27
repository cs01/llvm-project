// An atomic local pointer keeps the one-word __single default under Safe C
// instead of an automatic __bidi_indexable (pointer plus bounds, not atomic).
// With an attribute between _Atomic and the pointer, converting into the
// automatic one crashed; now the rule for pointers in _Atomic applies.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
int *_Nullable make(void);
int nullable(void) {
  _Atomic(int *_Nullable) p = make(); // expected-error {{a pointer in an '_Atomic' or other wrapper has no rule}}
  return 0;
}
int marked(void) {
  _Atomic(int *_Static) p = 0; // expected-error {{a pointer in an '_Atomic' or other wrapper has no rule}}
  return 0;
}
