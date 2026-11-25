// More of the third blind attack (holes/spatial): casts around incomplete structs,
// atomics through unchecked pointers, hand-written library declarations, vector
// lanes, builtins that check their own arguments, terminators in function types.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdatomic.h>
#include <stdlib.h>
struct O;
struct O *later(int *p) { return (struct O *)p; } // expected-error 1+ {{is not complete here}}
int *before(struct O *o) { return (int *)o; } // expected-error 1+ {{is not complete here}}
struct O { int *_Nullable a; };
void atomics(_Atomic int *__counted_by(n) p, int n, _Atomic int *one, int *e) {
  _Atomic int x = 0;
  int want = 0;
  atomic_store(&x, 1);
  atomic_store(one, 1);
  atomic_store(p + 2, 1);                             // expected-error 1+ {{not checked by an atomic operation}}
  atomic_store(&p[n], 1);                             // expected-error 1+ {{not checked by an atomic operation}}
  atomic_compare_exchange_strong(&x, &want, 1);
  atomic_compare_exchange_strong(&x, e, 1);
  atomic_compare_exchange_strong(&x, &n, 1);          // expected-error 1+ {{is the count of another field}}
}
void *memcpy(void *__sized_by(1) d, const void *__sized_by(1) s, unsigned long n); // expected-error {{is a library function; include its header}} expected-error 0+ {{}} expected-note 0+ {{}}
typedef int v4 __attribute__((vector_size(16)));
int lane(v4 a) { return a[4] + a[3]; } // expected-error {{vector}} expected-warning 0+ {{}}
void store(char *__counted_by(8) b, int i) { __builtin_nontemporal_store(42, b + i); } // expected-error {{accesses memory without any check}}
int slash(const char *__terminated_by('/') p) { return *p; }
typedef int (*fn)(const char *);
fn convert(void) { return slash; } // expected-error 1+ {{}}
