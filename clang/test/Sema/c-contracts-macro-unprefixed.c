// RUN: %clang_cc1 -fsyntax-only -internal-isystem %S/../../lib/Headers -fc-contracts -verify %s
// RUN: %clang_cc1 -fsyntax-only -internal-isystem %S/../../lib/Headers -std=c89 -Wall -Wno-comment -verify %s
// RUN: %clang_cc1 -E -P -DC_CONTRACTS_CPROVER -internal-isystem %S/../../lib/Headers %s | FileCheck %s

// expected-no-diagnostics

#define C_CONTRACTS_NO_PREFIX
#include <c_contracts.h>

unsigned fill(int *buf, unsigned len) {
  unsigned i = 0;
  unsigned writes = 0;
  while (i < len)
    assigns(locations(i, locations(writes, range(buf, 0, len))))
    loop_invariant(i <= len)
    decreases(len - i)
  {
    buf[i++] = 0;
    writes++;
  }
  return writes;
}

// CHECK: __CPROVER_assigns(i, writes, __CPROVER_object_upto((buf) + (0), ((len) - (0)) * sizeof(*(buf))))
// CHECK-NEXT: __CPROVER_loop_invariant(i <= len)
// CHECK-NEXT: __CPROVER_decreases(len - i)
