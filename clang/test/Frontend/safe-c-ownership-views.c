// '_View' structs: each pointer is as if it were a variable of its own.
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN:   -mllvm -safe-c-ownership=true -mllvm -safe-c-redeclared=false -verify %t/bodies.c
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN:   -mllvm -safe-c-ownership=true -mllvm -safe-c-redeclared=false -verify %t/decls.c

//--- bodies.c
#define _Owned [[clang::annotate_type("safec_owned")]]
#define _View __attribute__((annotate("safec_view")))
#define _Take(p) __builtin_safec_take(p)
void free(void *_Nullable);
struct node { int v; struct node *_Owned _Nullable next; };
struct list { struct node *_Owned _Nullable head; };
struct _View it { const struct node *_Nullable cur; int count; };

// Accepted.
struct it begin(const struct list *l) {
  struct it i = {l->head, 0};
  return i;
}
struct it next(struct it i) {
  if (i.cur)
    i.cur = i.cur->next;
  i.count++;
  return i;
}
int value(const struct it *i) { return i->cur ? i->cur->v : 0; }
int sum(const struct list *l) {
  int s = 0;
  for (struct it i = begin(l); i.cur; i = next(i))
    s += value(&i);
  return s;
}

// Rejected.
int field(struct list *l) {
  struct it i = {l->head, 0};
  free(_Take(l->head));
  return i.cur ? i.cur->v : 0; // expected-error {{after '_Take' on line}}
}
int result(struct list *l) {
  struct it i = begin(l);
  free(_Take(l->head));
  return value(&i); // expected-error {{after '_Take' on line}}
}
int copied(struct list *l) {
  struct it i = {l->head, 0}, j = i;
  free(_Take(l->head));
  return next(j).count; // expected-error {{after '_Take' on line}}
}
int scope(void) {
  struct it i = {0};
  {
    struct node n = {0};
    i.cur = &n; // expected-error {{'i.cur' outlives 'n'}}
  }
  return value(&i);
}
int through_call(void) {
  struct it r = {0};
  {
    struct node n = {0};
    struct it i = {&n, 0};
    r = next(i); // expected-error {{'r.cur' outlives 'n'}}
  }
  return value(&r);
}
struct it escapes(const struct list *l) {
  struct node n = {0};
  struct it i = {&n, 0}; // expected-error {{this returns a pointer to 'n', which ends with the function}}
  return i;
}

//--- decls.c
#define _Owned [[clang::annotate_type("safec_owned")]]
#define _View __attribute__((annotate("safec_view")))
struct _View two { const int *_Nullable a; const int *_Nullable b; };
struct _View owner { int *_Owned _Nullable p; }; // expected-error {{a '_View' holds borrows and numbers}}
struct holds { struct two t; }; // expected-error {{lives only in locals and parameters}}
static struct two global; // expected-error {{shared by the whole program}}
void set(struct two *t); // expected-error {{by value or as a pointer to 'const'}}
struct two make(void); // expected-error {{only if it has one pointer}}
const int *_Nullable pick(struct two t); // expected-error {{which pointer of the '_View'}}
int callback(int (*f)(const struct two *)); // expected-error {{this function pointer's type has no rule}}
