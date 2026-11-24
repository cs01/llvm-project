# Kernel idioms (design, 2026-11-24)

Goal: Safe C of value in the Linux kernel. A kernel file is ordinary C plus a few
idioms the current rules either reject or let through unchecked. Each idiom below
gets a rule, the machinery it reuses, and its Lean model before any compiler work
(the tagged-union and `_Drop` models each found a compiler hole when written
first).

## 1. `ERR_PTR`: a pointer or an error code

```c
struct file *f = filp_open(path, O_RDONLY, 0);
if (IS_ERR(f))
	return PTR_ERR(f);
f->f_pos = 0;          /* only valid after the IS_ERR check */
```

A function returns either a valid pointer or a negative errno cast to a pointer
(`(void *)-ENOMEM`). Dereferencing an error pointer is the same bug as a null
dereference, at an address near the top of memory.

- **Annotation:** `_Err_or` on a return type (or variable): the value may be an
  error pointer. `ERR_PTR(e)` has that type; `IS_ERR(p)` is the test.
- **Rule:** dereferencing, indexing, or passing to a non-`_Err_or` parameter
  needs `!IS_ERR(p)` known on the path, exactly like `_Nullable` needs a null
  check. `IS_ERR_OR_NULL(p)` narrows both.
- **Machinery:** the nullability flow analysis (`NullabilitySafety.cpp`) with a
  second fact per pointer ("may be an error"), narrowed by calls to `IS_ERR`
  and `IS_ERR_OR_NULL` recognized by name (they are inline functions in
  `err.h`). `PTR_ERR(p)` is allowed only where `IS_ERR(p)` is known.
- **Lean:** extend the null-safety core (`Sound.lean`) with an error value
  distinct from null; one hole per rule (deref without the test, test of the
  wrong pointer, a join that loses the narrowing).

## 2. `container_of`: from a member back to its struct

```c
struct item { int v; struct list_head node; };
struct item *it = container_of(pos, struct item, node);
```

The macro subtracts `offsetof(struct item, node)` from a pointer to the member.
It is correct only if `pos` really points at the `node` member of a `struct
item`. Plain casts like this are rejected today ("can treat memory as the wrong
type").

- **Annotation:** `_Embedded_in(struct item, node)` on the member's pointer type
  (or on the list head): this pointer always points at the `node` field of a
  `struct item`. `container_of` on such a pointer is checked: the type and
  member must match the annotation.
- **Where the promise comes from:** a pointer gets that type only from `&x->node`
  where `x` is a `struct item *` (or from another pointer of the same type). Any
  other source is an error. List traversal (`list_for_each_entry`) keeps the type
  because every `next` in a list of `_Embedded_in(struct item, node)` heads has it.
- **Bounds:** the result is `__single` (one `struct item`), with the bounds of the
  whole object, not of the member.
- **Lean:** a small model of structs with embedded heads: `container_of` is UB
  when the pointer does not point at that member; rules for where the annotated
  type may come from; holes for each.

## 3. `kmalloc` / `kfree`

`kmalloc`, `kzalloc`, `kcalloc`, `krealloc`, `kfree` and `kvfree` are the
allocator: mark them in a kernel `safec.h` section (`_Returns_owned`,
`ownership_takes`) so the M2 rules apply unchanged. `devm_*` allocations are freed
by the device core and are borrows tied to the device's lifetime: a later step.

## 4. `kref` and RCU (later)

- `kref_get`/`kref_put` hand out shared owners (Rust's `Arc`): needs a reference
  counted owner kind, after the M5 thread rules.
- RCU: readers between `rcu_read_lock`/`rcu_read_unlock` hold borrows that end at
  the unlock; writers publish with `rcu_assign_pointer`. Thread-safety capability
  attributes cover the lock part; the grace period needs its own rule.

## Order

1. `ERR_PTR` (Lean model, then the nullability extension): most common, and the
   machinery exists.
2. `kmalloc` family annotations (no compiler change).
3. `container_of` with `_Embedded_in` (Lean first).
4. A first kernel file ported (a small driver or `lib/` file) to measure.
5. `kref`, RCU.
