# Examples

[Back to the README](../README.md)

Each error is copied from the compiler (line numbers left out). They share:

```c
struct node { int v; struct node *_Owned _Nullable next; };
struct list { struct node *_Owned _Nullable head; };
void clear(struct list *l);
void node_free(struct node *_Owned _Nullable n);   /* frees n and the rest of its list */
```

## Code that compiles

```c
void node_free(struct node *_Owned _Nullable n) {
  if (!n) return;
  node_free(_Take(n->next));
  free(n);
}
void push(struct list *l, struct node *_Owned n) {
  node_free(_Take(n->next));   /* n's own list, if it brought one */
  n->next = _Take(l->head);
  l->head = n;
}
void clear_all(struct list *l) {
  while (l->head) {
    struct node *_Owned _Nullable n = _Take(l->head);
    if (!n) return;
    l->head = _Take(n->next);
    free(n);
  }
}
int total(const struct list *l) {
  int s = 0;
  for (const struct node *_Nullable n = l->head; n; n = n->next)
    s += n->v;
  return s;
}
```

## Code that does not

### 1. Freed after being given away

```c
void replace(struct list *l, struct node *_Owned n) {
  node_free(_Take(l->head));
  l->head = n;
  node_free(n);
}
```
```
error: 'n' is passed to 'node_free' after it was stored
```

### 2. A call frees what you hold

```c
int first(struct list *l) {
  struct node *_Nullable n = l->head;
  clear(l);
  return n ? n->v : 0;
}
```
```
error: 'n' is tested after the call to 'clear'
```

### 3. Freeing a list while walking it

```c
void each(struct list *l) {
  for (struct node *_Nullable n = l->head; n; n = n->next)
    node_free(_Take(l->head));
}
```
```
error: 'n' is used after '_Take'
```

### 4. Returning a pointer to the stack

```c
int *pair(int v) {
  int p[2] = {v, v};
  return p;
}
```
```
error: this returns a pointer to 'p', which ends with the function
```

### 5. Keeping somebody else's pointer

```c
struct cache { struct node *last; };
```
```
error: 'last' stores a borrow, which nothing can end when its owner does; if it frees what it
points to declare it '_Owned', if it is a literal or a global '_Static', otherwise pass the
pointer as a parameter or keep an index; a back pointer may be '_Unchecked'
```

### 6. The old pointer after `realloc`

```c
int grow(int *_Owned p, size_t n) {
  int *first = p;
  int *_Owned _Nullable q = realloc(p, n * sizeof *p);
  int r = *first;
  free(q);
  return r;
}
```
```
error: 'first' is read after it was passed to 'realloc' (which may free it)
```

### 7. Two names for one object

```c
void both(struct list *a, const struct list *b);
void alias(struct list *l) { both(l, l); }
```
```
error: two arguments of 'both' reach the same owner and it may change one; make that parameter
'const', or pass a copy
```

### 8. An iterator outlives its list

```c
struct _View it { const struct node *_Nullable cur; };
int stale(struct list *l) {
  struct it i = {l->head};
  node_free(_Take(l->head));
  return i.cur ? i.cur->v : 0;
}
```
```
error: 'i.cur' is tested after '_Take'
```

### 9. Following a back pointer

```c
struct tree { struct tree *_Owned _Nullable kid; struct tree *_Unchecked _Nullable up; int v; };
int parent(const struct tree *t) { return t->up ? t->up->v : 0; }
```
```
error: an '_Unchecked' pointer is followed only inside a '_Trusted' function
```

### 10. Wrong length

```c
int sum(const int *__counted_by(n) v, size_t n);
int short_by_one(void) {
  int x[3] = {1, 2, 3};
  return sum(x, 4);
}
```
```
error: passing array 'x' (which has 3 elements) to parameter 'v' ... with count value of 4 always fails
```

### 11. Null

```c
int value(const struct node *_Nullable n) { return n->v; }
```
```
error: dereference of nullable pointer
```

### 12. `malloc` in a signal handler

```c
void handler(int s) { free(malloc(4)); }
void install(void) { signal(SIGINT, handler); }
```
```
error: a signal may arrive in the middle of 'malloc' or 'printf', so a handler calls nothing but
'write' and '_exit'; set a 'volatile sig_atomic_t' and act on it later
```

## Text

[text.h](../safe-c-std/text.h) has the common string work, with no `__unsafe` in it: `str` is text someone else owns, `text` grows and is freed. It also
compiles as C99 with other compilers.

```c
long total = 0;
for (struct str rest = csv; rest.len; rest = str_after(rest, ',')) {
  str_long n = str_int(str_trim(str_before(rest, ',')));
  if (n.ok)
    total += n.value;
}
```

## Containers, files and options

[vec.h](../safe-c-std/vec.h), [map.h](../safe-c-std/map.h), [file.h](../safe-c-std/file.h) and [args.h](../safe-c-std/args.h), with no `__unsafe` in them:

```c
#define MAP_NAME counts
#define MAP_VALUE int
#include "map.h"

struct counts m = {0};
if (!counts_put(&m, STR("apple"), 1)) { counts_free(&m); return 1; }
int *_Nullable n = counts_get(&m, STR("apple"));
if (n) (*n)++;
counts_free(&m);
```

They hold plain data. What `_get` and `_at` return ends at the next call that changes the container.
