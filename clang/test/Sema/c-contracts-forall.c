// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover %s | FileCheck %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s
// expected-no-diagnostics

typedef unsigned long size_t;

// A property of every element of a collection. Clauses can otherwise name only
// scalars, members and contiguous slices, none of which can say this.
int all_zero(const char *p, size_t n)
  pre (readable(p, n))
  pre (forall (i : 0, n) p[i] == 0);
// CHECK:      /* all_zero */
// CHECK-NEXT: __CPROVER_requires(__CPROVER_r_ok(p, n))
// CHECK-NEXT: __CPROVER_requires(__CPROVER_forall { unsigned long i; (i < n) ==> (p[i] == 0) })

// A non-zero lower bound is emitted as a second conjunct; a zero one is not,
// because a redundant term is carried into the formula CBMC solves.
int tail_zero(const char *p, size_t lo, size_t n)
  pre (forall (i : lo, n) p[i] == 0);
// CHECK:      /* tail_zero */
// CHECK-NEXT: __CPROVER_requires(__CPROVER_forall { unsigned long i; (i >= lo && i < n) ==> (p[i] == 0) })

int explicit_signed_bound(const char *p, int lo, size_t n)
  pre (forall (i : (size_t)lo, n) p[i] == 0);

// The motivating case: nghttp2's HPACK ring buffer, where every live slot must
// hold a readable entry and nothing in the source says so.
struct entry { int v; };
struct ring { struct entry **buffer; size_t mask, first, len; };
struct entry *ring_get(struct ring *r, size_t idx)
  pre (idx < r->len)
  pre (r->len <= r->mask + 1)
  pre (forall (i : 0, r->len) readable(r->buffer[(r->first + i) & r->mask],
                                       sizeof(struct entry)))
  post (e: readable(e, sizeof(struct entry)));
// CHECK:      /* ring_get */
// CHECK:      __CPROVER_forall { unsigned long i; (i < r->len) ==> (__CPROVER_r_ok(r->buffer[(r->first + i) & r->mask], sizeof(struct entry))) }

// 'forall' is contextual: an ordinary identifier outside a contract predicate.
int forall = 3;
int use_forall(void) { return forall; }
