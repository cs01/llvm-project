// Flexible array holes of the third blind attack (holes/spatial d*, c019, t081):
// each ran out of bounds before.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdio.h>
#include <stdlib.h>
struct F { int n; int d[__counted_by(n)]; };
struct A { int k; };
struct V { int n; int *__counted_by(n) d; };
struct F *_Nullable make(int n) {
  struct F *f = malloc(sizeof *f + (size_t)n * sizeof(int));
  if (!f) return NULL;
  f->n = n;
  for (int i = 0; i < n; i++) f->d[i] = i;
  return f;
}
int get(struct F *f, int i) { return f->d[i]; }
int forged(struct A *a) { return get((struct F *__single)a, 6); } // expected-error 1+ {{nothing says the memory behind this pointer has the array}}
void aliased(void) { struct F *f = calloc(1, 12); if (!f) return; *(unsigned *)&f->n = 400; } // expected-error 1+ {{is the count of another field}}
void scanned(void) { struct F *f = calloc(1, 12); if (!f) return; sscanf("400", "%d", &f->n); } // expected-error 1+ {{is the count of another field}}
void retyped(struct F *f) { ((struct A *)f)->k = 400; } // expected-error 1+ {{keeps the length of its own array}}
struct G { int a; struct F f; }; // expected-error {{inside another struct that length is not checked}}
struct H { int n; };
struct N { struct H h; int d[__counted_by(h.n)]; };
void nested(void) { struct N *f = calloc(1, 12); if (!f) return; f->h.n = 400; } // expected-error 1+ {{holds the count of another field}}
int overridden(struct V v) {
  struct { struct V z; } w = {.z = v, .z.n = 5}; // expected-error {{replaces part of a value given whole}} expected-warning 0+ {{}} expected-note 0+ {{}}
  return w.z.d[4];
}
