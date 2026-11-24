// M4 step 2: a member of a tagged union is read or written only where the
// tag is known to name it: on the right edge of `if (x.tag == V)` (or its
// `!=` early return), in `case V:`, or after `x.tag = V`. A call that may
// modify the struct forgets what was known. The control run pins the M2/M4
// rules off.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

enum kind { CIRCLE, RECT };
struct shape {
  enum kind kind;
  union _Tagged_by(kind) {
    double radius _When(CIRCLE);
    double width _When(RECT);
  };
};
void reshape(struct shape *s);
void show(const struct shape *s);

double unchecked(const struct shape *s) {
  return s->radius; // expected-error {{reading 'radius' needs the tag 'kind' known to be CIRCLE here, or the union's bytes could be another member's; check it first (as in 'if (x.kind == CIRCLE)' or 'case CIRCLE:')}}
}

double checked(const struct shape *s) {
  if (s->kind == CIRCLE)
    return s->radius;
  return s->width; // expected-error {{reading 'width' needs the tag 'kind' known to be RECT here}}
}

double early_return(const struct shape *s) {
  if (s->kind != RECT)
    return 0;
  return s->width;
}

double by_case(const struct shape *s) {
  switch (s->kind) {
  case CIRCLE:
    return s->radius;
  case RECT:
    return s->width;
  }
  return 0;
}

double after_call(struct shape *s) {
  if (s->kind != CIRCLE)
    return 0;
  show(s);    // const, but *s may be a global that show changes by name
  double r = s->radius; // expected-error {{reading 'radius' needs the tag 'kind' known to be CIRCLE here}}
  reshape(s); // may change the tag
  return r + s->radius; // expected-error {{reading 'radius' needs the tag 'kind' known to be CIRCLE here}}
}

void set_then_write(struct shape *s) {
  s->kind = RECT;
  s->width = 2;
  s->radius = 1; // expected-error {{writing 'radius' needs the tag 'kind' known to be CIRCLE here}}
}

double locals(void) {
  struct shape a = {CIRCLE, {1.0}}; // the initializer sets the tag
  return a.radius;
}

// A tagged union may hold pointers: every access is checked against the tag,
// so an int's bytes are never read as a pointer.
struct value {
  int is_ptr;
  union _Tagged_by(is_ptr) {
    long n _When(0);
    int *_Static p _When(1);
  };
};
int deref(const struct value *v) {
  if (v->is_ptr == 1)
    return *v->p;
  if (v->is_ptr == 0)
    return (int)v->n;
  return 0;
}
// "not 1" does not mean 0: the tag could hold any int.
int not_one(const struct value *v) {
  if (v->is_ptr == 1)
    return 0;
  return (int)v->n; // expected-error {{reading 'n' needs the tag 'is_ptr' known to be 0 here}}
}
int pun(const struct value *v) {
  return *v->p; // expected-error {{reading 'p' needs the tag 'is_ptr' known to be 1 here}}
}

// A bool tag (as in a Result): `if (r.ok)`, `!r.ok`, `r.ok && r.value`, and
// the other edge of each, all name the member; messages say true/false.
struct res {
  _Bool ok;
  union _Tagged_by(ok) {
    long value _When(1);
    int err _When(0);
  };
};
long use_res(struct res r) {
  if (r.ok && r.value > 0)
    return r.value;
  if (!r.ok)
    return r.err;
  return r.value;
}
long use_res_bad(struct res r) {
  if (r.ok)
    return r.err; // expected-error {{reading 'err' needs the tag 'ok' known to be false here}}
  return 0;
}

// `(*s).kind` and `s->kind` are the same path (shared with the nullability
// analysis's paths).
double deref_form(const struct shape *s) {
  if ((*s).kind == RECT)
    return s->width;
  return 0;
}

// Three back doors to a member whose bytes a tag change reuses: a pointer to
// the member, a tag write through an alias, and a call given an alias.
int g_int = 42;
int member_address(void) {
  struct value x = {1, {.p = &g_int}};
  int *_Static *_Nullable pp = 0;
  if (x.is_ptr == 1)
    pp = &x.p; // expected-error {{a pointer to 'p' would outlive a change of the tag 'is_ptr', after which these bytes may be another member's (one that holds pointers); use the member by name, or copy its value out ('x = s.p;')}}
  return pp ? **pp : 0;
}
int alias_write(void) {
  struct value x = {1, {.p = &g_int}};
  struct value *q = &x;
  if (x.is_ptr != 1)
    return 0;
  q->is_ptr = 0;
  return *x.p; // expected-error {{reading 'p' needs the tag 'is_ptr' known to be 1 here}}
}
void clobber(struct value *w);
int alias_call(void) {
  struct value x = {1, {.p = &g_int}};
  struct value *q = &x;
  if (x.is_ptr != 1)
    return 0;
  clobber(q);
  return *x.p; // expected-error {{reading 'p' needs the tag 'is_ptr' known to be 1 here}}
}
int unaliased_local_survives_calls(void) {
  struct value x = {1, {.p = &g_int}};
  struct value other = {0, {.n = 0}};
  if (x.is_ptr != 1)
    return 0;
  clobber(&other); // x's address was never taken: its tag cannot change
  return *x.p;
}
