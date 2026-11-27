// A local initialized from a terminated array (not a pointer) crashed the
// inference that makes 'char *s = terminated_ptr' terminated: an array has no
// pointee type. Now bounds-safety's own rule for the conversion applies.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
struct s { char g[__null_terminated 4]; };

int first(void) {
  struct s x = {"abc"};
  char *p = x.g; // expected-error-re {{initializing 'char *__bidi_indexable' with an expression of incompatible type 'char[__terminated_by(0) 4]' {{.*}}requires a linear search}} \
                 // expected-note 3 {{consider}}
  return p[0];
}
