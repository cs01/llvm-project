// M2: each pointer kind holds only what it promises. A __safec_static points
// only at static data; an __safec_unchecked is used only inside __unsafe; a
// __safec_view is never stored in a global or a non-view field, and is written
// only by name. The control run (no -safe-c-ownership) accepts everything.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s
// The rule is off here, so the __unsafe block below suppresses nothing.

int counter;
static const char text[] = "abc";
struct entry { const char *_Nullable __safec_static name; int *_Nullable __safec_unchecked raw; };

struct entry table[] = {{"one", 0}, {text + 1, &counter}};
const char *__safec_static greeting = "hello";

void set(struct entry *e, const char *s, int *p) {
  e->name = "two";
  e->name = &text[1];
  e->name = greeting;
  e->name = s; // expected-error {{'name' is '__safec_static', so it may point only at string literals, globals or functions, which are never freed; this value may point at memory that is; make 'name' '__safec_owned' if it is responsible for freeing what it points to}}
  struct entry local = {s, p}; // expected-error {{'name' is '__safec_static'}}
  e->raw = p; // ok: anything converts to unchecked
}

int use(struct entry *e, struct entry *f) {
  f->raw = e->raw; // ok: copy between unchecked
  if (e->raw == 0)
    return 0;
  int *q = e->raw; // expected-error {{'raw' is '__safec_unchecked': nothing tracks whether its target is still alive, so its value can be used only inside '__unsafe' (or copied into another '__safec_unchecked'); if this struct owns the target, make 'raw' '__safec_owned' instead}}
  int v = 0;
  __unsafe { v = *e->raw; } // off-warning {{unsafe block contains nothing}}
  return *q + v;
}

struct __safec_view span { const int *__counted_by(n) p; int n; };
struct holder { struct span s; }; // expected-error {{field 's' holds a '__safec_view' struct, whose borrows are tracked only in locals and parameters; make it a local, or pass it as a parameter}}
struct span global_span; // expected-error {{global 'global_span' holds a '__safec_view' struct}}

void views(struct span *sp, const int *__counted_by(4) a) {
  struct span v;
  v.n = 4;
  v.p = a;
  sp->n = 0;
  sp->p = a; // expected-error {{writing a borrow into a '__safec_view' through a pointer can outlive what it points to, because the view's own lifetime is unknown here; write the field of a local view by name ('v.p = ...')}}
}
