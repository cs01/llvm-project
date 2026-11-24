// Safe C lifetime elision (Rust's rule): a function returning a borrowed
// pointer or view with exactly one borrowed parameter returns a borrow of
// it, without _Lifetimebound. Not elided: two borrowed parameters, a
// _Noescape parameter, owned results (_Returns_owned). The control run turns
// the ownership rules off, so the caller-side error goes away.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify=off %s

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct node { int v; struct node *_Owned _Nullable next; };

struct node *_Nullable next(struct node *n) { return n->next; }
_Returns_owned struct node *_Nullable make(const struct node *like) {
  (void)like;
  return malloc(sizeof(struct node));
}
int *_Nullable pick(int *a, int *b) { (void)b; return a; } // expected-error {{'pick' returns a pointer borrowed from 'a'}} expected-note {{param returned here}} off-error {{'pick' returns a pointer borrowed from 'a'}} off-note {{param returned here}}

int use(struct node *_Exclusive h) {
  struct node *_Nullable d = next(h); // expected-error {{'next' returns a borrow of 'h'}}
  free(_Take(h->next));               // expected-note {{the borrow ends here}}
  return d ? d->v : 0;                // expected-note {{later used here}}
}

int owned(struct node *h) {
  struct node *_Nullable m = make(h);
  int v = m ? 1 : 0;
  free(m);
  return v;
}
