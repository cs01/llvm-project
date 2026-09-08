// Verifies the portable annotation layer in c_contracts.h: the same source is
// checked as contracts under -fc-contracts and vanishes entirely without it.

// RUN: %clang_cc1 -fsyntax-only -internal-isystem %S/../../lib/Headers -fc-contracts -verify=aware %s
// RUN: %clang_cc1 -fsyntax-only -internal-isystem %S/../../lib/Headers -std=c89 -Wall -Wno-comment -verify=blind %s
// RUN: %clang_cc1 -fsyntax-only -internal-isystem %S/../../lib/Headers -fc-contracts -fcontract-emit-cprover %s | FileCheck %s
// RUN: %clang_cc1 -E -P -DC_CONTRACTS=1 -internal-isystem %S/../../lib/Headers %s -o %t.i
// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit %t.i | FileCheck %s --check-prefix=UNIT
//
// The same source targeted at CBMC's own front end, with no contract-aware
// compiler in the pipeline. Verified end to end against goto-cc/cbmc; this
// checks the expansion, which is the part that can rot.
// RUN: %clang_cc1 -E -P -DC_CONTRACTS_CPROVER -internal-isystem %S/../../lib/Headers %s | FileCheck %s --check-prefix=CPROVER

// blind-no-diagnostics

#include <c_contracts.h>

typedef unsigned long size_t;

void put(int *buf, unsigned len, unsigned i, int v)
  c_pre     (buf != 0)
  c_pre     (i < len) // aware-note {{precondition declared here}}
  c_assigns (buf[i]);

// UNIT: void put(int *buf, unsigned len, unsigned i, int v)
// UNIT-NEXT: __CPROVER_requires(buf != 0)
// UNIT-NEXT: __CPROVER_requires(i < len)
// UNIT-NEXT: __CPROVER_assigns(buf[i]);

// CHECK: /* put */
// CHECK-NEXT: __CPROVER_requires(buf != 0)
// CHECK-NEXT: __CPROVER_requires(i < len)
// CHECK-NEXT: __CPROVER_assigns(buf[i])

size_t decode(void *dst, size_t dstCap, const void *src, size_t srcSize)
  c_pre     (c_readable(src, srcSize))
  c_pre     (c_writable(dst, dstCap))
  c_assigns (c_range((char *)dst, 0, dstCap))
  c_returns (c_result <= c_old(dstCap));

// CHECK: /* decode */
// CHECK-NEXT: __CPROVER_requires(__CPROVER_r_ok((src), (srcSize)))
// CHECK-NEXT: __CPROVER_requires(__CPROVER_w_ok((dst), (dstCap)))
// CHECK-NEXT: __CPROVER_assigns(__CPROVER_object_upto(((char *)dst), (dstCap)))
// CHECK-NEXT: __CPROVER_ensures(__CPROVER_return_value <= __CPROVER_old(dstCap))

// CPROVER: size_t decode(void *dst, size_t dstCap, const void *src, size_t srcSize)
// CPROVER-NEXT: __CPROVER_requires(__CPROVER_r_ok((src), (srcSize)))
// CPROVER-NEXT: __CPROVER_requires(__CPROVER_w_ok((dst), (dstCap)))
// CPROVER-NEXT: __CPROVER_assigns(__CPROVER_object_upto(((char *)dst) + (0), ((dstCap) - (0)) * sizeof(*((char *)dst))))
// CPROVER-NEXT: __CPROVER_ensures(__CPROVER_return_value <= __CPROVER_old(dstCap));

// A role is one annotation that lowers to several clauses.
size_t decode_role(void *dst, size_t dstCap, const void *src, size_t srcSize)
  c_writes  (dst, dstCap)
  c_reads   (src, srcSize)
  c_returns (c_result <= c_old(dstCap));

// UNIT: size_t decode_role(void *dst, size_t dstCap, const void *src, size_t srcSize)
// UNIT-NEXT: __CPROVER_requires((dst) != 0)

// CHECK: /* decode_role */
// CHECK-NEXT: __CPROVER_requires((dst) != 0)
// CHECK-NEXT: __CPROVER_requires(__CPROVER_w_ok((dst), (dstCap)))
// CHECK-NEXT: __CPROVER_assigns(__CPROVER_object_upto(((char *)(dst)), (dstCap)))
// CHECK-NEXT: __CPROVER_requires((src) != 0)
// CHECK-NEXT: __CPROVER_requires(__CPROVER_r_ok((src), (srcSize)))
// CHECK-NEXT: __CPROVER_ensures(__CPROVER_return_value <= __CPROVER_old(dstCap))

// An _n role counts elements of a typed pointer; the extent is in bytes.
// A read-modify-write buffer composes the two roles rather than naming a third.
void scale(int *buf, size_t len) c_reads_n (buf, len) c_writes_n (buf, len);

// CHECK: /* scale */
// CHECK-NEXT: __CPROVER_requires((buf) != 0)
// CHECK-NEXT: __CPROVER_requires(__CPROVER_r_ok((buf), (len) * sizeof (*(buf))))
// CHECK-NEXT: __CPROVER_requires((buf) != 0)
// CHECK-NEXT: __CPROVER_requires(__CPROVER_w_ok((buf), (len) * sizeof (*(buf))))
// CHECK-NEXT: __CPROVER_assigns(__CPROVER_object_upto((buf), (len) * sizeof(*(buf))))

unsigned hash(const void *src, size_t n) c_reads (src, n) c_writes_nothing; // aware-note {{precondition declared here}}

// CHECK: /* hash */
// CHECK: __CPROVER_assigns()

// CPROVER: unsigned hash(const void *src, size_t n)
// CPROVER-SAME: __CPROVER_requires((src) != 0)
// CPROVER-SAME: __CPROVER_requires(__CPROVER_r_ok((src), (n)))
// CPROVER-SAME: __CPROVER_assigns()

void fill(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    c_assigns   (c_locations(i, c_range(buf, 0, len)))
    c_invariant (i <= len)
    c_decreases (len - i)
  {
    buf[i] = 0;
    i++;
  }
}

// CHECK: __CPROVER_loop_invariant(i <= len)
// CHECK-NEXT: __CPROVER_decreases(len - i)
// CPROVER: __CPROVER_assigns(i, __CPROVER_object_upto((buf) + (0), ((len) - (0)) * sizeof(*(buf))))

int all_nonzero(const int *buf, size_t len)
  c_reads_n (buf, len)
  c_pre (c_forall(i, 0, len, buf[i] != 0));

// CHECK: __CPROVER_requires(__CPROVER_forall { unsigned long i; (i < len) ==> ((buf[i] != 0)) })
// CPROVER: __CPROVER_requires(__CPROVER_forall { unsigned long i; ((i) >= (0) && (i) < (len)) ==> (buf[i] != 0) });

void caller(void) {
  int b[16];
  // aware-warning@+1 {{precondition i < len of 'put' is violated by this call}}
  put(b, 8, 8, 1);
  // aware-warning@+1 {{precondition (src) != 0 of 'hash' is violated by this call}}
  hash(0, 1);
}

// A predicate that arrives through a macro has no source text to quote, so the
// diagnostic prints the expansion rather than a placeholder.
void take(int *buf, size_t len) c_writes_n (buf, len); // aware-note {{precondition declared here}}

void null_caller(void) {
  // aware-warning@+1 {{precondition (buf) != 0 of 'take' is violated by this call}}
  take(0, 4);
}
