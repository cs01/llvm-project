// M2 rule 10: a struct field or global holds no borrow. Its pointers are
// _Owned, _Static or _Unchecked, unless the struct is a
// _View. Staged behind -mllvm -safe-c-ownership; the control run
// (Safe C without it) accepts everything.
// RUN: %clang_cc1 -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

struct node {
  int v;
  struct node *_Nullable next; // expected-error {{field 'next' holds a pointer nothing tracks, so it can outlive what it points to; declare it '_Owned' if it is responsible for freeing its target, or '_Static' if it only points at string literals, globals or functions, or mark the struct '_View' if it lives only in locals and parameters}}
  struct node *_Nullable _Owned child;
  void (*cb)(int);
  int *_Nullable slots[4]; // expected-error {{field 'slots' holds a pointer nothing tracks}}
};

struct _View span { const int *__counted_by(n) p; int n; };

struct entry { const char *_Static name; int *_Unchecked raw; };

const char *banner = "hi"; // expected-error {{global 'banner' holds a pointer nothing tracks, so it can outlive what it points to; declare it '_Owned' if it is responsible for freeing its target, or '_Static' if it only points at string literals, globals or functions}}
const char *_Static motd = "hi";
static int *_Nullable _Owned cache;
void (*_Nullable handler)(void);

int f(const int *p) {
  const int *q = p; // locals may borrow
  static const int *_Nullable last; // expected-error {{global 'last' holds a pointer nothing tracks}}
  return *q;
}
