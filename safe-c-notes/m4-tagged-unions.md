# M4: tagged unions and exhaustive switch (design, 2026-11-23)

Goal: Rust's `enum` with data, `match` and `Result`, spelled in plain C. Reading the
wrong member of a union is type confusion; today Safe C allows only unions whose
members hold no pointers. M4 makes unions with pointers safe when they say which
member is live.

## Spelling (safec.h; nothing on other compilers)

```c
struct shape {
  enum shape_kind kind;
  union _Tagged_by(kind) {
    struct circle c _When(CIRCLE);
    struct rect   r _When(RECT);
  };
};
```

- `_Tagged_by(f)`: on a union that is a member (named or anonymous) of a
  struct; `f` is a sibling field of enum or integer type.
- `_When(V, ...)`: on each union member; the tag values for which it is live.
  A value may name at most one member. Members without `_When` are errors.
- Both are decl attributes (annotate), like `_View`: a union member is never
  an inner pointer, so no type attribute is needed.

`Result` is the two-member case with a `bool ok` tag (`_When(true)` and
`(false)`), declared by `SAFEC_RESULT(name, T, E)`.

## Rules

1. **Read narrowing.** Reading `s.c` (or `p->c`) needs the tag known to be `CIRCLE`
   at that point: inside `if (s.kind == CIRCLE)`, the matching `case`, or after an
   early return on `!=`. Same flow machinery as null narrowing (nullsafe), keyed by
   the tag field's path. Where it cannot be proven, the read is an error; no run-time
   check is inserted (a trap here would just be a crash with a better message).
2. **Write sets the tag.** Writing a member needs the tag written to that member's
   value in the same full statement or initializer: compound literal or designated
   initializer `{ .kind = RECT, .r = {...} }`, or `shape_set_rect(&s, r)` generated
   by a macro. A bare `s.r = x` is an error (the tag would lie).
3. **Tag writes end narrowing** and, when the union holds owned pointers, are
   ownership operations (M2): changing the tag of a union whose live member owns
   memory must take it first, or it leaks / dangles.
4. **Exhaustive switch.** A `switch` on a tag field must cover every enumerator that
   some member is `_When` for, or have `default`. Built on `-Wswitch` (made an
   unsilenceable error for tag switches only, so plain enum switches keep C rules).
5. **No punning.** Taking the address of a member (`&s.c`) gives a borrow that is
   ended by any tag write (M2 flow layer: the tag field is the loan root).
6. **Copying** a tagged struct copies tag and payload together: allowed, unless the
   payload has owned fields (M2 rule 15).

## Implementation

1. Sema: parse/validate the attributes (tag is a sibling, values distinct, every
   member covered); rule 2 at assignments and initializers.
2. Flow: extend nullsafe's narrowing facts with "tag path == V"; rule 1 at member
   reads. This is the main work.
3. Rule 4 in `SemaStmt` switch checking.
4. Rules 3 and 5 reuse the M2 loans (tag field as root).
5. Lit test per rule with control run; then `Result` + `TRY` in the stdlib; then the
   census projects with tagged unions (Lua `TValue`, SQLite `Mem`) as the gate.

## Open questions (decided by claude unless the user objects)

- Integer tags with `#define` values are allowed (`_When` takes constant
  expressions), so existing C compiles after annotation without introducing enums.
- No run-time trap fallback for unprovable reads: an error with "check `kind` first"
  is the pit of success, and it keeps the rule static like Rust's `match`.
