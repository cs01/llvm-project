// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover %s | FileCheck %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts %s | FileCheck -check-prefix=OFF -allow-empty %s
// OFF-NOT: __CPROVER

// The mapping to CBMC is close to one to one, which is the argument for
// targeting an existing verifier rather than building one: 'pre' is
// __CPROVER_requires, 'post' is __CPROVER_ensures with the result binding
// renamed, and 'old' is __CPROVER_old.
unsigned long decompress(void *dst, unsigned long dstCap, const void *src)
  pre  (dst != 0)
  pre  (dstCap > 0)
  post (r: r <= old(dstCap));
// CHECK:      /* decompress */
// CHECK-NEXT: __CPROVER_requires(dst != 0)
// CHECK-NEXT: __CPROVER_requires(dstCap > 0)
// CHECK-NEXT: __CPROVER_ensures(__CPROVER_return_value <= __CPROVER_old(dstCap))

int *allocate(unsigned long n) pre (n > 0) post (r: r != 0);
// CHECK:      /* allocate */
// CHECK-NEXT: __CPROVER_requires(n > 0)
// CHECK-NEXT: __CPROVER_ensures(__CPROVER_return_value != 0)

// The result name is substituted as a whole token: 'rate' contains 'r' but is
// not it.
int rates(int n) pre (n > 0) post (r: r > 0);
// CHECK:      /* rates */
// CHECK-NEXT: __CPROVER_requires(n > 0)
// CHECK-NEXT: __CPROVER_ensures(__CPROVER_return_value > 0)

// A frame condition prints from its target list, comma separated, and composes
// with the clauses either side of it.
void repack(int *buf, unsigned long cap)
  pre  (buf != 0)
  assigns (buf[0], cap);
// CHECK:      /* repack */
// CHECK-NEXT: __CPROVER_requires(buf != 0)
// CHECK-NEXT: __CPROVER_assigns(buf[0], cap)

// The empty frame is a specification, not an absent clause.
int reads_only(const int *p) assigns ();
// CHECK:      /* reads_only */
// CHECK-NEXT: __CPROVER_assigns()
