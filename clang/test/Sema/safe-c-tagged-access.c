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
  show(s);    // const: cannot change the tag
  double r = s->radius;
  reshape(s); // may change the tag
  return r + s->radius; // expected-error {{reading 'radius' needs the tag 'kind' known to be CIRCLE here}}
}

void set_then_write(struct shape *s) {
  s->kind = RECT;
  s->width = 2;
  s->radius = 1; // expected-error {{reading 'radius' needs the tag 'kind' known to be CIRCLE here}}
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
