// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

// 'old' names the value a parameter had at function entry. Without it a 'ensures'
// naming a by-value parameter is ambiguous, since the body may mutate its copy.
int decode(void *dst, unsigned cap) ensures (r: r <= old(cap));

// The design's headline example, which the first draft wrote without old().
int is_error(int);
unsigned long decompress(void *dst, unsigned long dstCap)
  requires (dst != 0)
  ensures  (r: r <= old(dstCap) || is_error(r)); // expected-error {{contract predicate must be free of side effects}}
// expected-note@8 {{mark 'is_error' 'const' or 'pure' to allow calling it from a contract predicate}}

int is_err_pure(int) __attribute__((pure));
unsigned long decompress_ok(void *dst, unsigned long dstCap)
  requires (dst != 0)
  ensures  (r: r <= old(dstCap) || is_err_pure((int)r));

// A bare parameter reference is still rejected, and the note points at old().
int bare(int n) ensures (r: r <= n); // expected-error {{'ensures' predicate cannot name parameter 'n' directly; a by-value parameter may be named in 'ensures' only through 'old()'}}
// expected-note@-1 {{name the value at function entry with 'old(n)'}}

// 'old' has no meaning in a 'requires': there is no earlier state to name.
int in_pre(int n) requires (old(n) > 0); // expected-error {{'old' may only appear in an 'ensures' contract clause}}

// Scalars only. Snapshotting anything larger has no cheap lowering.
struct Big { int a[16]; };
int big(struct Big b) ensures (r: old(b).a[0] == 0); // expected-error {{'old' requires a scalar operand; 'struct Big' is not scalar}}

// A pointer is a scalar, so its entry value can be named.
int ptr_entry(int *p) ensures (r: old(p) != 0);

// 'old' is contextual: outside a predicate it is an ordinary identifier.
int old = 7;
int uses_old(void) { return old; }
