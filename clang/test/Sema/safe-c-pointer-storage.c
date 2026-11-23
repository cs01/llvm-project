// M2 rule 10: a struct field or global holds no borrow. Its pointers are
// __safec_owned, __safec_static or __safec_unchecked, unless the struct is a
// __safec_view. Staged behind -mllvm -safe-c-ownership; the control run
// (Safe C without it) accepts everything.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

struct node {
  int v;
  struct node *_Nullable next; // expected-error {{field 'next' holds a pointer nothing tracks, so it can outlive what it points to; declare it '__safec_owned' if it is responsible for freeing its target, or '__safec_static' if it only points at string literals, globals or functions, or mark the struct '__safec_view' if it lives only in locals and parameters}}
  struct node *_Nullable __safec_owned child;
  void (*cb)(int);
  int *_Nullable slots[4]; // expected-error {{field 'slots' holds a pointer nothing tracks}}
};

struct __safec_view span { const int *__counted_by(n) p; int n; };

struct entry { const char *__safec_static name; int *__safec_unchecked raw; };

const char *banner = "hi"; // expected-error {{global 'banner' holds a pointer nothing tracks, so it can outlive what it points to; declare it '__safec_owned' if it is responsible for freeing its target, or '__safec_static' if it only points at string literals, globals or functions}}
const char *__safec_static motd = "hi";
static int *_Nullable __safec_owned cache;
void (*_Nullable handler)(void);

int f(const int *p) {
  const int *q = p; // locals may borrow
  static const int *_Nullable last; // expected-error {{global 'last' holds a pointer nothing tracks}}
  return *q;
}
