# Examples

[Back to the README](../README.md)

## Examples

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

This works for local variables and through calls annotated `__safec_owned` or
`__safec_takes`. Through struct fields, arrays and globals it is not caught yet (see the
status table).
