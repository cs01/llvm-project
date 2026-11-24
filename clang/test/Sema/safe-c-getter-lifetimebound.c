// M2 rule 9: a function returning a borrow (here, of an owned field) must
// mark the parameter it comes from _Lifetimebound, so callers keep the
// borrow tied to it. The control run is plain bounds safety, which does not
// run the lifetime analysis.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include safec.h -verify=off %s
// off-no-diagnostics

struct T { int *_Owned _Nullable item; };
// With two pointer parameters nothing is elided: say which one.
int *_Nullable get(struct T *t, struct T *u) { (void)u; return t->item; } // expected-error {{'get' returns a pointer borrowed from 't', so it is valid only as long as what 't' points to; mark the parameter '_Lifetimebound' so callers keep it alive, or return an owned copy}} expected-note {{param returned here}}
int *_Nullable get2(struct T *t _Lifetimebound) { return t->item; }

// __unsafe around the return lifts the error, as for other Safe C errors.
int *_Nullable get3(struct T *t, struct T *u) { (void)u; __unsafe { return t->item; } }
// One pointer parameter: the result borrows it (Rust's lifetime elision).
int *_Nullable get4(struct T *t) { return t->item; }
