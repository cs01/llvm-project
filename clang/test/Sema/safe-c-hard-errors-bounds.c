// Bounds-safety errors that are mappable warnings (DefaultError) cannot be
// downgraded either.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -Wno-error=bounds-safety-strict-terminated-by-cast \
// RUN:   -Wno-bounds-safety-strict-terminated-by-cast -verify %s
// Without -fsafe-c the same flags downgrade and silence it.
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety \
// RUN:   -Wno-error=bounds-safety-strict-terminated-by-cast \
// RUN:   -Wno-bounds-safety-strict-terminated-by-cast -verify=off %s
// off-no-diagnostics
#include <ptrcheck.h>
typedef __SIZE_TYPE__ size_t;
struct S { const char *__null_terminated name; };
// A stored terminated pointer still needs the explicit conversion (only call
// arguments get the implicit checked one).
void store(struct S *s, const char *__counted_by(n) buf, size_t n) {
  s->name = buf; // expected-error {{is an unsafe operation}}
}
