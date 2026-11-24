// M4 step 1: tagged unions. _Tagged_by names the sibling field holding the
// tag; each member says with _When which tag values make it live; a switch
// on the tag must handle every value or have a default. (Reading a member is
// checked against the tag in step 2.) The control run is plain clang, where
// the annotations expand to nothing.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s

enum kind { CIRCLE, RECT, SQUARE, TRIANGLE };
struct circle { double r; };
struct rect { double w, h; };

struct shape {
  enum kind kind;
  union _Tagged_by(kind) {
    struct circle c _When(CIRCLE);
    struct rect r _When(RECT, SQUARE);
  };
};

double area(const struct shape *s) {
  switch (s->kind) { // expected-error {{this switch on 'kind' does not handle SQUARE, a tag value of the union it describes; add 'case SQUARE:' (or 'default:')}} expected-warning {{enumeration value 'SQUARE' not handled in switch}} expected-note {{add missing switch cases}} plain-warning {{enumeration value 'SQUARE' not handled in switch}} plain-note {{add missing switch cases}}
  case CIRCLE:
    return 3.14 * s->c.r * s->c.r;
  case RECT:
    return s->r.w * s->r.h;
  case TRIANGLE:
    return 0;
  }
  return 0;
}

double area_ok(const struct shape *s) {
  switch (s->kind) {
  case CIRCLE:
    return s->c.r;
  case RECT:
  case SQUARE:
    return s->r.w;
  default:
    return 0;
  }
}

struct bad1 {
  int k;
  union _Tagged_by(tag) { int a _When(0); } u; // expected-error {{the union is tagged by 'tag', but the struct has no field of that name; name the field that holds the tag, as in '_Tagged_by(kind)'}}
};
struct bad2 {
  double k; // expected-error {{the tag 'k' must be an integer or enum field, so its value can say which member is live}}
  union _Tagged_by(k) { int a _When(0); } u;
};
struct bad3 {
  int k;
  union _Tagged_by(k) {
    int a _When(0);
    long b; // expected-error {{member 'b' of a tagged union does not say when it is live; add '_When(VALUE)' with the tag values for which it holds data}}
    short c _When(0); // expected-error {{tag value 0 is claimed by both 'a' and 'c'; each value must name one live member}}
  } u;
};

// Unit variants: with an enum tag, every enumerator is a variant, so a
// variant without data needs no member, and a switch must still handle it.
struct parse_error {
  enum { EMPTY, BAD_CHAR, OVERFLOW } kind;
  union _Tagged_by(kind) {
    struct { unsigned at; char c; } bad _When(BAD_CHAR);
  };
};
int describe(const struct parse_error *e) {
  switch (e->kind) { // expected-error {{this switch on 'kind' does not handle OVERFLOW}} expected-warning {{enumeration value 'OVERFLOW' not handled in switch}} expected-note {{add missing switch cases}} plain-warning {{enumeration value 'OVERFLOW' not handled in switch}} plain-note {{add missing switch cases}}
  case EMPTY:
    return 0;
  case BAD_CHAR:
    return e->bad.c;
  }
  return -1;
}
