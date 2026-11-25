# Examples

> Being rewritten. `_Exclusive`, `_View`, `_Unchecked`, `_Takes`, `_Returns_owned`, `_Lifetimebound`, `_Drop` and
> `_Unsynchronized` no longer exist; [annotations](annotations.md) is current.

[Back to the README](../README.md)

## Eight classic C bugs, as compile errors

Each one compiles without a word under a normal C compiler. Errors are copied from the
compiler, shortened where marked `...`.

### 1. A view into a string, then the string grows

```c
string s _Drop(string_free) = {0, 0, 0};
string_push(&s, S("hello"));
str first = string_view(&s);
string_push(&s, S(" world")); // may realloc
write_all(1, first);
```
```
error: 'string_view' returns a borrow of '&s', which ends ... before this pointer's
last use; call 'string_view' again after that point, or finish using the pointer first
```

### 2. Use after free in a linked list

```c
struct node *_Nullable d = h->next;
free(_Take(h->next));
return d ? d->v : 0;
```
```
error: this pointer is borrowed from 'h->next', and the borrow ends (the field is
taken, ...) before its last use; read 'h->next' again after that point, ...
```

### 3. Double free: two owners of one block

```c
int *_Nullable o = malloc(4);
a.p = o;
b.p = o;
free(_Take(a.p));
free(_Take(b.p));
```
```
error: object allocated by 'malloc' does not live long enough
note: local variable 'o' aliases the storage of object allocated by 'malloc'
```

### 4. Returning a pointer to the stack

```c
int *pair(int v) { return (int[]){v, v}; }
```
```
error: stack memory associated with compound literal is returned
```

### 5. Off by one

```c
int a[4] = {1, 2, 3, 4};
SLICE(int) v = {a, 5};
```
```
error: initializing 'v.p' ... and count value of 5 with array 'a' (which has 4
elements) always fails
```

### 6. Using a result without checking it

```c
i64_result r = str_to_i64(s);
return r.value;
```
```
error: reading 'value' needs the tag 'ok' known to be true here, ...; check it first
(as in 'if (x.ok)')
```

### 7. A thread given a pointer into the caller's stack

```c
static void *worker(void *arg) { ... return NULL; }

int local = 42;
pthread_t __single t; // __single: a macOS SDK workaround, for now
pthread_create(&t, NULL, worker, &local);
```
```
error: the new thread may outlive what this pointer borrows (the spawning function can
return or free it first); give the thread memory it owns (a new allocation, moved in)
or static data
```

### 8. Shared data without its lock

```c
struct mtx stats_lock;
int hits _Guarded_by(stats_lock);
void count(void) { hits++; }
```
```
error: writing variable 'hits' requires holding mutex 'stats_lock' exclusively
```

## More examples

### Null: unchecked `malloc`

```c
int *m = malloc(sizeof(int));
// error: dereference of nullable pointer
*m = 1;
```

```c
int *m = malloc(sizeof(int));
if (!m) return -1;
*m = 1;  // ok: checked
```

### Bounds: wrong length at the call site

```c
int sum(const int *__counted_by(n) a,
        size_t n);

int x[3];
// error: passing array 'x' (which has
// 3 elements) ... with count value of 4
sum(x, 4);
sum(x, 3);  // ok
```

### Bounds: indexing a pointer that has no length

```c
// error: array subscript on single
// pointer 'p' must use a constant index
// of 0
void f(int *p) { p[1] = 0; }
```

```c
void f(int *__counted_by(n) p, size_t n) {
  if (n > 1)
    p[1] = 0;  // ok
}
```

### Bounds: off by one at run time

```c
int sum(const int *__counted_by(n) a,
        size_t n) {
  int s = 0;
  for (size_t i = 0; i <= n; i++)
    s += a[i];  // traps at i == n
  return s;
}
```

The trap is like a Rust panic.

### Use after free

```c
// error: object allocated by 'malloc'
// does not live long enough
int *p = malloc(sizeof(int));
free(p);   // note: freed here
*p = 1;    // note: later used here
```

Through struct fields too. A pointer read out of an owned field is a borrow, and it ends
when the field is taken, replaced, or the struct is lent to a function that may change it:

```c
struct node *first = l->head;
// void clear(struct list *_Exclusive l);
clear(l);    // note: the borrow ends here
first->v;    // error: this pointer is
             // borrowed from 'l->head'
```
