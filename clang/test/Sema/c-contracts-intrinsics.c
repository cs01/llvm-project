// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover %s | FileCheck %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify -DERRORS %s

// The questions a contract needs to ask about a pointer are the ones C has no
// way to ask, so they are spelled as ordinary calls that need no declaration.
// They exist only inside a clause, which is what keeps them from being new
// reserved words.

typedef unsigned long size_t;

void f(const void *src, void *dst, size_t n)
  pre (readable(src, n))
  pre (writable(dst, n))
  pre (same_object(src, dst))
  pre (pointer_offset(src) >= 0);
// CHECK:      /* f */
// CHECK-NEXT: __CPROVER_requires(__CPROVER_r_ok(src, n))
// CHECK-NEXT: __CPROVER_requires(__CPROVER_w_ok(dst, n))
// CHECK-NEXT: __CPROVER_requires(__CPROVER_same_object(src, dst))
// CHECK-NEXT: __CPROVER_requires(__CPROVER_POINTER_OFFSET(src) >= 0)

// 'readable' is a lower bound and does not constrain the object above it;
// 'fresh' gives an object of exactly n bytes. Different questions, so both.
void g(const void *p, size_t n) pre (fresh(p, n));
// CHECK:      /* g */
// CHECK-NEXT: __CPROVER_requires(__CPROVER_is_fresh(p, n))

// Outside a clause they are nothing special: an ordinary undeclared call.
#ifdef ERRORS
int outside(const void *p) {
  return readable(p, 1); // expected-error {{call to undeclared function 'readable'}}
}
#endif

// A codebase with its own 'readable' keeps it: the intrinsic is reached only
// after ordinary lookup has failed, so this resolves to the declaration below
// and is rejected for the ordinary reason.
__attribute__((const)) int usable(void);
int mine(void) pre (usable());
// CHECK:      /* mine */
// CHECK-NEXT: __CPROVER_requires(usable())

// A local of the same name is read, not called, so it is not rewritten.
int shadowed(int readable) pre (readable > 0);
// CHECK:      /* shadowed */
// CHECK-NEXT: __CPROVER_requires(readable > 0)
