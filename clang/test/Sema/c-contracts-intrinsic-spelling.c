// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s
//
// ISSUE 9: anyone arriving from CBMC writes __CPROVER_r_ok before they write
// 'readable', and the purity error is a confusing way to be told so.

int deref(int *p)
    // expected-error@+3 {{call to undeclared function '__CPROVER_r_ok'}}
    // expected-error@+2 {{contract predicate must be free of side effects}}
    // expected-note@+1 {{use the contract intrinsic 'readable' rather than CBMC's '__CPROVER_r_ok'}}
    pre(__CPROVER_r_ok(p, sizeof(int)))
{
  return *p;
}

int deref2(int *p)
    // expected-error@+3 {{call to undeclared function '__CPROVER_nonsense'}}
    // expected-error@+2 {{contract predicate must be free of side effects}}
    // expected-note@+1 {{contract intrinsics are 'readable', 'writable', 'fresh', 'same_object', 'pointer_in_range' and 'pointer_offset'}}
    pre(__CPROVER_nonsense(p))
{
  return *p;
}

// The grammar spelling is accepted.
int deref_ok(int *p)
    pre(readable(p, sizeof(int)))
{
  return *p;
}
