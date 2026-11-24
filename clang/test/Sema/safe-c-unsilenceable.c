// Safe C's safety errors cannot be turned off: -w and -Wno-<group> leave
// them errors, and an earlier bounds error does not stop the analyses that
// find later bugs (here the lock check in the last function). The control
// run is plain C with -w: nothing is reported.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -w -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -Wno-lifetime-safety \
// RUN:   -Wno-thread-safety -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -w -verify=off %s
// off-no-diagnostics

int *escape(void) {
  int x = 1;
  return &x; // expected-error {{stack memory associated with local variable 'x' is returned}} expected-note {{returned here}}
}

int count_too_big(void) {
  int a[4] = {1, 2, 3, 4}; // expected-note {{'a' declared here}}
  int *__counted_by(5) p = a; // expected-error {{always fails}}
  return p[0];
}

struct _Capability("mutex") mtx { int state; };
struct mtx stats_lock;
int hits _Guarded_by(stats_lock);
void bump(void) { hits++; } // expected-error {{writing variable 'hits' requires holding mutex 'stats_lock' exclusively}}
