// Safe C accepts only what it has rules for: kinds of statements, expressions
// and casts, attributes and pragmas are on lists (safe-c-docs/rules.md), and
// anything else is an error (2026-11-24; before, unknown meant accepted).
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fenable-matrix -ferror-limit=0 -fsyntax-only -Xclang -verify %s
union u { int i; float f; };
int goto_label(int i) {
  void *_Static t[] = {&&a, &&b}; // expected-error 2 {{no rules for this ('AddrLabelExpr')}} expected-error 0+ {{}}
  goto *t[i];                     // expected-error {{no rules for this ('IndirectGotoStmt')}} // expected-warning 0+ {{}}
a:
  return 1;
b:
  return 2;
}
int to_union(int i) { union u x; x = (union u)i; return x.i; } // expected-error {{no rules for this ('ToUnion')}}
int bits(float f) { return __builtin_bit_cast(int, f); }       // expected-error 2 {{no rules for this}}
typedef float m4 __attribute__((matrix_type(2, 2)));           // expected-error {{no rules for the attribute 'matrix_type'}}
int space(int __attribute__((address_space(1))) *p);           // expected-error {{no rules for the attribute 'address_space'}}
__attribute__((target("crc"))) int tgt(void) { return 1; }     // expected-error {{no rules for the attribute 'target'}}
#pragma redefine_extname foo bar // expected-error {{no rules for '#pragma redefine_extname foo'}}
#pragma clang section bss = "__DATA,x" // expected-error {{no rules for '#pragma clang section'}}
#pragma clang diagnostic push
#pragma clang diagnostic pop
#pragma pack(push, 1)
#pragma pack(pop)
// Ordinary code stays accepted.
struct arr { int a[2]; };
struct arr mk(void) { return (struct arr){{1, 2}}; }
__attribute__((always_inline, unused)) static inline int ok(int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    switch (i) { case 0: s++; __attribute__((fallthrough)); default: s += i ?: 1; }
  int t = ({ int x = s; x * 2; });
  return _Generic(t, int: t, default: 0) + mk().a[0] + (int)_Alignof(long);
}
