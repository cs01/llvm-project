# Out arguments and view arrays in safe-c-lean

A Lean model of Safe C's rules for a callee that stores a borrow where another
argument points, and for heap arrays of `_View` structs:

```c
void set(int **out, int *in) { *out = in; }
set(&o, b); free(b); *o;                        /* rejected at the caller */

struct tok *_Owned t = calloc(n, sizeof *t);    /* struct _View tok { const char *text; } */
t[0].text = b; free(b); *t[0].text;             /* rejected */
```

Programs are lists of functions. A call is checked from the callee's
signature (parameter types and `_Noescape`) only, and each body is checked on
its own, as in the compiler. There is a soundness theorem for the full rule
set and, for every rule, a program that passes the checker without that rule
and reaches UB.

```
lake build      # green, no sorry, no warnings
```

## Files

- `SafeC/Out.lean`: syntax, the big-step semantics, the `Rules` switches and
  the checker (`chk` for statements, `chkCall`, `chkFn`, `check`).
- `SafeC/OutSound.lean`: the invariant, the proof, the main theorem, and
  `#print axioms out_sound`.
- `SafeC/OutHoles.lean`: one or more counterexamples per rule and the accepted
  idioms, all checked by `decide` in the kernel.

## The theorem

```lean
theorem out_sound (P : List Fn) (h : check Rules.full P = true) (fuel : Nat) :
    (run P fuel).isUB = false
```

If every body in `P` passes the checker under the full rules, running function
0 (`main`, no parameters) never reaches UB, for any fuel. Recursion is allowed.
The UB kinds are `useAfterFree`, `doubleFree` and `deadLocal` (reaching a
variable of a frame that has returned). What is left is `ok`, `trap` (a null
dereference, or a null view array passed as an out argument) or `outOfFuel`.
`#print axioms` lists only `propext`, `Classical.choice` and `Quot.sound`.

### Semantics

Memory is a list of one-cell blocks, and a variable's storage is a block, so
`&x` and a heap pointer are the same kind of value. Variables are `int`,
`int *`, `int *_Owned` (also a struct whose field `p` that is), `struct tok
*_Owned`, an alias `struct tok *q` of one such variable, and the parameters
`int *`, `out` (`int **` or `struct tok *`), `struct own *` and `struct own
*_Exclusive`. An out argument is `&x`, a view array `t`, or the caller's own
out parameter. Every free is `free(_Take(...))`, which leaves null behind.
There is one global owner, and one global pointer to views.

### Proof idea

The checker keeps, per variable, the loans its value may come from (for a view
array or an out parameter, what the cell it points to holds): `own v`, what
`v` owns; `addr v`, a local int; `par v`, what parameter `v` brought in;
`glob`. The invariant: the value is null or a live block that one of the loans
stands for. Owners never share a block, so a free ends exactly the loans of
one owner.

A frame also has a frame condition (`Eff`): below its own variables it rewrites
only the owner cells of its exclusive parameters and of the global, frees only
what they held, and writes only the cells its out parameters point to. The
call case uses that, and what the callee's exit check says about its out
cells and result. It never looks into the callee's body.

## Rules, and what each counterexample shows

`hole R P k`: `P` is accepted by `R`, rejected by `Rules.full`, and runs to `k`.

| Rule | Counterexamples | UB |
|---|---|---|
| 1 `flowOut` | `need_flowOut` | use after free |
| 2 `noEscapeChecked` | `need_noEscapeChecked`, `_local`, `_call`, `_field` | use after free |
| 3 `interior` | `need_interior`, `need_interior_ex` | use after free |
| 4 `noGlobalOut` | `need_noGlobalOut` | use after free |
| 4 `noGlobalReturn` | `need_noGlobalReturn` | use after free |
| 5 `noLocalOut` | `need_noLocalOut` | dead local |
| 6 `viewFlow` | `need_viewFlow` | use after free |
| 7 `viewOwnerOnly` | `need_viewOwnerOnly` | use after free |
| 8 `viewNoField` | `need_viewNoField` | use after free |

The proof needed five more rules that the list of eight does not state:

| Rule | What it says | Counterexamples |
|---|---|---|
| 9 `argsApart` | The borrows an argument carries, directly or through an out cell or view array, must still be usable after the exclusive arguments of the same call have been lent; no object is lent exclusively and passed again | `need_argsApart`, `_out`, `_twice` |
| 10 `callEndsGlobal` | A call ends every borrow of what a global owns, including those its own arguments carry | `need_callEndsGlobal` |
| 11 `outsMayAlias` | A store through one out parameter adds to every other one | `need_outsMayAlias` |
| 12 `exitLive` | What an out parameter points to must still be usable at return | `need_exitLive` |
| 13 `aliasCurrent` | A read through an alias of a view array sees what the array borrows now, not what it did when the alias was made | `need_aliasCurrent` |

Two conditions are built into the types rather than rules with a switch: an
out argument or a struct argument always names the variable it reaches, and a
global is never passed as a struct argument. The compiler has neither (see
below).

Idioms accepted under the full rules: `use_first_ok`, `not_kept_ok`,
`field_ok`, `views_ok`, `fill_ok`, `alias_read_ok`, `id_ok`, `relay_ok`.

## Programs the compiler accepts (2026-11-24, before any fix)

Each condition above was turned into C and tried on the compiler built that
day (`-fsafe-c -fsyntax-only`; the three Sema tests for these rules pass on
it). These compile without a diagnostic, and AddressSanitizer reports a use
after free (or, for the last variant of the first row, after return). `N` is
`_Nullable __single`.

| Condition | Program |
|---|---|
| An out argument names the variable it reaches | `set(c ? &o : &o2, b)`, `set((c, &o), b)`, `set(({&o;}), b)`, `set(id(&o), b)`, `set(&(c ? &v : &v2)->p, b)`, and `pp = &o2; pp = &o; set(pp, b)`; in a body, `p = c ? out : out; set(p, &x)` |
| Same, for a struct argument (rule 3) | `set_field(&o, c ? &s : &s2)`; `struct _View V { struct own *s; }` with `*out = v.s->p` |
| Writes through a pointer to a borrow reach its variable | `int *N *p = &o; *p = b;`, `*(c ? &o2 : &o) = b`; in a body, `int *N *p = out;` then `*p = in` (`_Noescape`), `*p = g.p`, or `*p = s->p; free(_Take(s->p))` |
| Every pointer to a borrow in the signature is an out argument | `struct _View W { int *N *out; }` passed by value or as `const struct W *`; `int *N *const *w` with `**w = in`; `int *N **w` with `**w = in` |
| Every argument that carries borrows is a source | `void f(struct box *_Owned t, int *N *out) { *out = t[0].p; free(t); }` |
| `noLocalOut` for what the body owns by parameter | `void f(int *N *out, int *_Owned q) { *out = q; free(q); }` |
| `noEscapeChecked` against the signature callers see | `_Noescape` on the first declaration, not on the definition; `*id(out) = in` |
| `exitLive` | `set(c ? out : out2, s->p); free(_Take(s->p));` |
| `argsApart` | `i = s.p; f(i, &s)`; `f(&q, &s)` with `q` or a view or a view array holding `s.p`; `f(v, &s)`; `a = &s; f(a, &s)`; `f(id(&s), &s)`; `f(get(&s), &s)`; `f(v, &s)` with `v.s == &s`; `f(q, b)` and `f(t, b)` with `b` given to an `_Owned` parameter |
| A global is not passed as a struct argument | `void f(struct own *s) { l = s->p; drop(); *l; }` called as `f(&g)` |
| `noGlobalReturn` for callers that do not see the body | `fp = get_static; p = fp(); free(_Take(g.p)); *p;` |
| `viewFlow` follows the views when they move | `u = _Take(t)`, `u = realloc(_Take(t), n)` |
| `aliasCurrent` | `q = t; t[0].p = b; free(b); *q[0].p;`, also through `&t[1]`, through a call that reads `*q`, for `q = &v` with a local view, and for `int *N *q = &o; o = b; free(b); **q` |

## What the model does not cover

- **One level.** An out parameter points straight at a cell that holds a
  borrow. No `int ***`, no view that holds a pointer to a borrow or to a
  struct, no pointer to an out cell kept in a local.
- **Arguments are variables.** No `c ? &o : &o2`, no call results and no
  reassigned pointer as an argument. The compiler has to resolve those; the
  model says what it must resolve them to.
- **Part of rule 7.** An out parameter cannot be reassigned here, so the
  "never reassigned" condition is in the syntax. The model has the alias half
  (`need_viewOwnerOnly`). Field by field and whole-view writes are one
  statement, since a view has one cell.
- **Part of rule 4.** There are no `static` functions: every body with a
  global borrow in its result is rejected. The exception that inlines the
  body's effect into callers that see it is not modeled.
- **Rule 8 covers globals only.** A struct field that points to views is not
  modeled; the global stands for it.
- **No `_Lifetimebound`.** A returned pointer may carry any non-`_Noescape`
  argument, so the result is treated like one more out cell.
- **No owned parameters.** Moving ownership into a call or out through a
  return, moving a view array to another variable, and `realloc` are not
  modeled.
- **`_Noescape` exempts the pointer and, for a struct pointer, its interior.**
  What an out parameter points to is always a source.
- **A borrow is a pointer to an int.** Arrays have one element, views one
  field, structs one owned field.
- **Globals are coarse.** One global owner; every call ends its borrows.
- **No loops**, no `goto`, no conditions the checker reads, no early return,
  no function pointers, no threads. `malloc` never fails. Leaks are not UB.
- **The checker is not the compiler.** It is flow-sensitive with a plain union
  at joins, like the lifetime analysis, but it is rewritten here from the
  rules. The theorem says nothing about the C++ code.
