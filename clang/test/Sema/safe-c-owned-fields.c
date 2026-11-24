// M2 rules 3, 5 and 15: an owned field is given only pointers it may free,
// is freed only after moving it out with __safec_take, and a struct holding
// one is not copied (both copies would free it). The control run (no
// -safe-c-ownership) accepts everything except the misuse of __safec_take,
// which is checked whenever the builtin is used.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);

struct node { int v; struct node *_Nullable __safec_owned next; };
struct list { struct node *_Nullable __safec_owned head; size_t len; };
struct node *_Nullable __safec_owned spare;

void push(struct list *l, struct node *_Nullable __safec_owned n) {
  if (!n)
    return;
  n->next = __safec_take(l->head);
  l->head = n;
}

void bad_stores(struct list *l, struct node *borrowed) {
  struct node *_Nullable fresh = malloc(sizeof(struct node));
  l->head = fresh;
  l->head = borrowed; // expected-error {{'head' is '__safec_owned', so it must be given a pointer it may free: a new allocation, an owned local (which is then moved), or '__safec_take(...)' of another owned field; this value is borrowed, so its owner would free it too}}
  struct node *_Nullable h = l->head;
  if (h)
    h->next = l->head; // expected-error {{'next' is '__safec_owned'}}
  spare = __safec_take(l->head);
}

void frees(struct list *l) {
  free(l->head); // expected-error {{freeing a pointer that is still stored in 'head' leaves it dangling; move it out first, as in 'free(__safec_take(n->next))', where the place is '__safec_owned'}}
  free(spare);   // expected-error {{freeing a pointer that is still stored in 'spare'}}
  free(__safec_take(l->head));
  free(__safec_take(spare));
}

struct list make(void) {
  struct list l = {0, 0};
  return l; // ok: returning a local moves it
}

void copies(struct list *a) {
  struct list b = *a; // expected-error {{copying 'struct list' duplicates its owned field 'head', and both copies would free it; pass a pointer to it instead, or move the field out with '__safec_take'}}
  (void)b;
  struct list c = make(); // ok: a call result is not a copy
  (void)c;
}

struct counted { int *_Nullable __safec_owned __counted_by(n) data; size_t n; };
void counted_take(struct counted *c) { free(__safec_take(c->data)); }

struct nn { int *__safec_owned p; };
void take_nonnull(struct nn *x) {
  (void)__safec_take(x->p); // expected-error {{'__safec_take' leaves null behind, so 'p' must be declared '_Nullable'}}
  // off-error@-1 {{'__safec_take' leaves null behind}}
}

// Only named locals are followed by the lifetime analysis: an allocation in
// an array element or behind a pointer needs an owned place, and is freed
// only after moving it out.
void arrays(void) {
  int *_Nullable arr[2] = {0, 0};
  arr[0] = malloc(sizeof(int)); // expected-error {{a new allocation stored in an array element is not tracked, so nothing stops a use after it is freed; declare the place '__safec_owned' (for an array: 'T *__safec_owned a[N]'), or keep the pointer in a local variable}}
  free(arr[0]); // expected-error {{freeing a pointer that is still stored in an array element leaves it dangling}}
  int *__safec_owned _Nullable own[2] = {0, 0};
  own[0] = malloc(sizeof(int));
  free(__safec_take(own[0]));
}
void destroy(int *_Nullable *pp) {
  free(*pp); // expected-error {{freeing a pointer that is still stored in memory reached through a pointer}}
}
void destroy_owned(int *__safec_owned _Nullable *pp) { free(__safec_take(*pp)); }
void destroy_borrowed(int *_Nullable *pp) {
  free(__safec_take(*pp)); // expected-error {{memory reached through a pointer is borrowed, so its owner will free what it points to too; declare it '__safec_owned' if this code is responsible for freeing it}}
}
