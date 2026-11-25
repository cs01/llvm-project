// The pointer-kind checks walk struct initializers; after an error in one,
// its slot is empty, which must not crash (cmark's chunk.h did). Unnamed
// bit-fields have no slot. The control run is Safe C without ownership.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s

struct chunk { const char *_Static data; int : 3; int len; };
struct chunk lit(const int *data) {
  // expected-error@+3 {{initializing}} off-error@+3 {{initializing}}
  // expected-note@-2 {{consider adding}} off-note@-2 {{consider adding}}

  struct chunk c = {data, 1};
  return c;
}
struct chunk ok = {"x", 1};
