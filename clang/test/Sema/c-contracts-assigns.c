// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

struct S { int a; int b; };

// A frame condition is a list of locations, not a predicate: the commas
// separate targets rather than forming one comma operator.
void fill(int *buf, unsigned len, struct S *s)
  assigns (buf[0], *buf, s->a, len);

// The empty frame says the function modifies nothing. That is a specification,
// not an error.
int pure_query(const int *p) assigns ();

// A target must name something that can be written to.
void bad_value(int n)
  assigns (n + 1); // expected-error {{'assigns' target must name a location that can be modified}}

void bad_call(int *p)
  assigns (p + 1); // expected-error {{'assigns' target must name a location that can be modified}}

// Evaluating a target must not change the state it describes.
void bad_effect(int *p)
  assigns (*p++); // expected-error {{contract predicate must be free of side effects}}

// 'assigns' composes with the other clauses, in any order.
unsigned long decompress(void *dst, unsigned long cap)
  pre  (dst != 0)
  assigns (cap)
  post (r: r <= old(cap));

// 'assigns' is contextual: a function may still be named after it.
int assigns(int n);

// A loop takes a frame too. CBMC needs one before it will discharge a loop
// invariant: applying the invariant means havocking what the loop writes, so it
// has to be told what that is.
void zero(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    assigns        (i, buf[i])
    loop_invariant (i <= len)
    decreases      (len - i)
  {
    buf[i] = 0;
    i++;
  }
}

// The same checks apply on a loop as on a function.
void loop_bad_target(int n) {
  int i = 0;
  while (i < n)
    assigns (n + 1) // expected-error {{'assigns' target must name a location that can be modified}}
  {
    i++;
  }
}

// A frame may name a half-open range of elements: buf[lo] through buf[hi - 1].
// Half-open so the bound is the one already written in the loop header.
void zero_range(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    assigns        (i, buf[0 : len])
    loop_invariant (i <= len)
    decreases      (len - i)
  { buf[i] = 0; i++; }
}

// A non-zero lower bound is fine, and so is a range on a function declarator.
void fill_tail(int *buf, unsigned lo, unsigned hi) assigns (buf[lo : hi]);

// A range needs something to take elements of.
void bad_base(int n) assigns (n[0 : 4]); // expected-error {{'assigns' range needs a pointer or array to take elements of; 'int' is neither}}

// Bounds are integers.
void bad_bound(int *buf, double d) assigns (buf[0 : d]); // expected-error {{'assigns' range bound must be an integer; 'double' is not}}

// And they cannot move the state they describe.
int tick(void);
void impure_bound(int *buf) assigns (buf[0 : tick()]); // expected-error {{contract predicate must be free of side effects}}
