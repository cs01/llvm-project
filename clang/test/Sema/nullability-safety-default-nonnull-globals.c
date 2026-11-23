// Under -fnullability-default=nonnull an unannotated pointer with static
// storage means _Nonnull, so it must start non-null and stay non-null.
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nonnull -verify %s
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nullable -verify=nullable %s
// nullable-no-diagnostics

#define NULL ((void *)0)

static int x;
static int *g_ok = &x;
static int *g_null = NULL; // expected-warning {{initializing a variable of nonnull type 'int *' with null}}
static int *g_uninit;      // expected-warning {{initializing a variable of nonnull type 'int *' with null}}
extern int *g_extern;      // defined elsewhere: no initializer here is fine
static int *_Nullable g_nullable;

void store(int *_Nullable p) {
  g_ok = p; // expected-warning {{assigning nullable pointer to nonnull variable 'g_ok'}} expected-note {{add a null check before assigning}}
  g_nullable = p;
  if (p)
    g_ok = p;
}

int read_nullable(void) { return g_nullable ? *g_nullable : 0; }

void local_static(void) {
  static int *s; // expected-warning {{initializing a variable of nonnull type 'int *' with null}}
  (void)s;
}
