// M2 rules 3, 5 and 15: an owned field is given only pointers it may free,
// is freed only after moving it out with _Take, and a struct holding
// one is not copied (both copies would free it). The control run (no
// -safe-c-ownership) accepts everything except the misuse of _Take,
// which is checked whenever the builtin is used.
// RUN: %clang_cc1 -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);

struct node { int v; struct node *_Nullable _Owned next; };
struct list { struct node *_Nullable _Owned head; size_t len; };
struct node *_Nullable _Owned spare;

void push(struct list *_Exclusive l, struct node *_Nullable _Owned n) {
  if (!n)
    return;
  n->next = _Take(l->head);
  l->head = n;
}

void bad_stores(struct list *_Exclusive l, struct node *borrowed) {
  struct node *_Nullable fresh = malloc(sizeof(struct node));
  l->head = fresh;
  l->head = borrowed; // expected-error {{'head' is '_Owned', so it must be given a pointer it may free: a new allocation, an owned local (which is then moved), or '_Take(...)' of another owned field; this value is borrowed, so its owner would free it too}}
  struct node *_Nullable h = l->head; // expected-error {{this pointer is borrowed from 'l->head'}}
  if (h)
    h->next = l->head; // expected-error {{'next' is '_Owned'}} expected-error {{replacing an owned field through 'h' changes what its owner holds}} expected-note {{the borrow ends here}} expected-note {{later used here}}
  spare = _Take(l->head);
}

void frees(struct list *_Exclusive l) {
  free(l->head); // expected-error {{freeing a pointer that is still stored in 'head' leaves it dangling; move it out first, as in 'free(_Take(n->next))', where the place is '_Owned'}}
  free(spare);   // expected-error {{freeing a pointer that is still stored in 'spare'}}
  free(_Take(l->head));
  free(_Take(spare));
}

struct list make(void) {
  struct list l = {0, 0};
  return l; // ok: returning a local moves it
}

void copies(struct list *a) {
  struct list b = *a; // expected-error {{copying 'struct list' duplicates its owned field 'head', and both copies would free it; pass a pointer to it instead, or move the field out with '_Take'}}
  (void)b;
  struct list c = make(); // ok: a call result is not a copy
  (void)c;
}

struct counted { int *_Nullable _Owned __counted_by(n) data; size_t n; };
void counted_take(struct counted *_Exclusive c) { free(_Take(c->data)); }

struct nn { int *_Owned p; };
void take_nonnull(struct nn *x) {
  (void)_Take(x->p); // expected-error {{'_Take' leaves null behind, so 'p' must be declared '_Nullable'}}
  // off-error@-1 {{'_Take' leaves null behind}}
}

// Only named locals are followed by the lifetime analysis: an allocation in
// an array element or behind a pointer needs an owned place, and is freed
// only after moving it out.
void arrays(void) {
  int *_Nullable arr[2] = {0, 0};
  arr[0] = malloc(sizeof(int)); // expected-error {{a new allocation stored in an array element is not tracked, so nothing stops a use after it is freed; declare the place '_Owned' (for an array: 'T *_Owned a[N]'), or keep the pointer in a local variable}}
  free(arr[0]); // expected-error {{freeing a pointer that is still stored in an array element leaves it dangling}}
  int *_Owned _Nullable own[2] = {0, 0};
  own[0] = malloc(sizeof(int));
  free(_Take(own[0]));
}
void destroy(int *_Nullable *pp) {
  free(*pp); // expected-error {{freeing a pointer that is still stored in memory reached through a pointer}}
}
void destroy_owned(int *_Owned _Nullable *_Exclusive pp) { free(_Take(*pp)); }
void destroy_borrowed(int *_Nullable *_Exclusive pp) {
  free(_Take(*pp)); // expected-error {{memory reached through a pointer is borrowed, so its owner will free what it points to too; declare it '_Owned' if this code is responsible for freeing it}}
}

// fopen's result is owned (fclose frees it), and a compound literal is a new
// object, not a copy of an owning struct.
typedef struct FILE_ FILE;
FILE *_Nullable fopen(const char *path, const char *mode);
struct reader { FILE *_Owned _Nullable f; struct list items; };
void open_reader(struct reader *_Exclusive r) {
  r->f = fopen("x", "r");
  r->items = (struct list){0, 0};
}
