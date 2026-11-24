// A null check on a __counted_by_or_null field narrows it like a plain
// _Nullable field, although bounds safety wraps the later access in opaque
// values and a bounds check. Found porting cJSON.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
struct buf { unsigned char *_Nullable __counted_by_or_null(len) p; size_t len; }; // expected-error {{holds a pointer nothing tracks}}
int use(const unsigned char *q);

int checked(struct buf *b) {
  if (b->p == 0)
    return 0;
  return use(b->p);
}

int unchecked(struct buf *b) {
  return use(b->p); // expected-error {{passing nullable pointer to nonnull parameter}} expected-note {{add a null check}}
}
