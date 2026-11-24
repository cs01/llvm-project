// Under -fsafe-c, indexing a local initialized from a single-element pointer
// (an unannotated field, return value or parameter) is an error, not a warning
// followed by a trap: the fix is a __counted_by on that field or return.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -verify=plain %s

typedef __SIZE_TYPE__ size_t;
struct holder {
  int *data; // expected-note {{holder::data declared here}} plain-note {{holder::data declared here}} expected-error {{holds a pointer nothing tracks}}
};

int field(struct holder *h, size_t i) {
  int *p = h->data; // expected-note {{pointer 'p' initialized here}} plain-note {{pointer 'p' initialized here}}
  return p[i];      // expected-error {{indexing into a __bidi_indexable local variable 'p' initialized from __single struct member 'data' results in a trap if the index expression is >= 1}} \
                    // plain-warning {{indexing into a __bidi_indexable local variable 'p' initialized from __single struct member 'data' results in a trap if the index expression is >= 1}}
}
