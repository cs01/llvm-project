// An index that contains an earlier error is value-dependent and cannot be
// evaluated; the single-pointer subscript check asserted on it. Found
// compiling BearSSL under -fsafe-c: a container-of macro's out-of-bounds
// pointer arithmetic is rejected, and the result is used inside an index.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify -verify-ignore-unexpected=note %s
typedef __SIZE_TYPE__ size_t;
struct dn { int x; };
struct eng { int a; int cpu; };
// (The field 'tas' is a borrow; the ownership check reports that, but it does not
// run after the errors below.)
struct srv { struct eng eng; struct dn *tas; size_t cur_dn_index; };
#define ENG ((struct eng *)(void *)((unsigned char *)t0ctx - __builtin_offsetof(struct eng, cpu)))
#define CTX ((struct srv *)ENG)
void run(void *t0ctx) {
  const struct dn *d;
  d = &CTX->tas[CTX->cur_dn_index]; // expected-error 2 {{can treat memory as the wrong type}} expected-error 2 {{pointer arithmetic on single pointer}}
  (void)d;
}
