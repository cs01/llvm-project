// Callbacks, constant tables, '_Static', realloc and the library's out
// parameters under the ownership rules (SafeCOwnership.cpp).
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -isystem %t/sys -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN: -mllvm -safe-c-ownership=true -mllvm -safe-c-redeclared=false -verify %t/a.c

//--- sys/sig.h
void (*_Nullable signal(int, void (*_Nullable)(int)))(int);

//--- a.c
#define _Owned [[clang::annotate_type("safec_owned")]]
#define _Static [[clang::annotate_type("safec_static")]]
#define _Take(p) __builtin_safec_take(p)
typedef unsigned long size_t;
void *_Nullable calloc(size_t, size_t) __attribute__((alloc_size(1, 2)));
void *_Nullable malloc(size_t) __attribute__((alloc_size(1)));
void *_Nullable realloc(void *_Nullable, size_t) __attribute__((alloc_size(2)));
void free(void *_Nullable);
#include <sig.h>

struct node { int v; struct node *_Owned _Nullable next; };
struct list { struct node *_Owned _Nullable head; };

// Accepted.
static const char *const days[] = {"mon", "tue"};
struct op { const char *_Static name; int code; };
static const struct op ops[] = {{"add", 1}, {"sub", 2}};
const char *_Static name(int i) { return ops[i].name; }
const char *_Static day(int i) {
  static const char *const short_days[] = {"m", "t"};
  return i ? days[i] : short_days[0];
}
void rename_op(struct op *o) { o->name = name(1); }
void each(const struct node *_Nullable n, void (*f)(const struct node *, int *), int *out) {
  for (; n; n = n->next)
    f(n, out);
}
void sum(const struct node *n, int *out) { *out += n->v; }
void all(const struct list *l, int *out) { each(l->head, sum, out); }
int grow(void) {
  int *_Owned _Nullable p = malloc(16);
  int *_Owned _Nullable q = realloc(p, 32);
  free(q);
  return 0;
}
typedef int sig_atomic_t;
static volatile sig_atomic_t hits;
void on_signal(int s) { hits = s; }

void my_free(void *_Owned _Nullable p) { free(p); }
int wrapped(char *_Owned _Nullable p, int c) {
  char *_Nullable q = c ? p : 0;
  int r = q != 0;
  my_free(p);
  return r;
}

// Rejected.
int wrapped_stale(char *_Owned _Nullable p, int c) {
  char *_Nullable q = c ? p : 0;
  my_free(p);
  return q != 0; // expected-error {{after passed to 'my_free'}}
}
int call_changes(void (*f)(struct list *), struct list *l) {
  struct node *_Nullable n = l->head;
  f(l);
  return n ? n->v : 0; // expected-error {{after the call to 'the function pointer' on line}}
}
void reassigned(void (*f)(int)) {
  f = on_signal; // a function of the same type and marks
}
void handler_calls(int s) { free(malloc(4)); }
void installs(void) {
  signal(2, handler_calls); // expected-error {{a handler calls nothing but 'write' and '_exit'}}
}
const char *_Static not_static(const char *_Owned p) {
  return p; // expected-error {{a '_Static' result is a literal or the address of a global}}
}
void stores_borrow(struct op *o, const char *p) {
  o->name = p; // expected-error {{a '_Static' field holds only literals}}
}
