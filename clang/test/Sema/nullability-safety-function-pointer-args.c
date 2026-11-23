// Arguments of a call through a function pointer are checked against the
// pointer's prototype, as direct calls are against the callee.
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nonnull -verify %s
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -verify=unspec %s

#define NULL ((void *)0)

static void sink(int *p) { (void)*p; }
static void sink_nullable(int *_Nullable p) { (void)p; }
static void sink_explicit(int *_Nonnull p) { (void)*p; }

void unannotated_param(void) {
  void (*f)(int *) = sink;
  int *data = NULL;
  f(data); // expected-warning {{passing nullable pointer to nonnull parameter 1}} expected-note {{add a null check before the call}}
}

void nullable_param(void) {
  void (*f)(int *_Nullable) = sink_nullable;
  f(NULL);
}

void explicit_nonnull(int *_Nullable q) {
  void (*f)(int *_Nonnull) = sink_explicit;
  f(q); // expected-warning {{passing nullable pointer to nonnull parameter 1}} expected-note {{add a null check before the call}} \
        // unspec-warning {{passing nullable pointer to nonnull parameter 1}} unspec-note {{add a null check before the call}}
}

void checked(int *_Nullable q) {
  void (*f)(int *) = sink;
  if (q)
    f(q);
}
