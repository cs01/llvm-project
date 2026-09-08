// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit \
// RUN:   -fcontract-emit-harness -verify %s | FileCheck -match-full-lines %s

// The harness is the one artifact an author should never write by hand: a
// hand-written __CPROVER_assume is an assumption nobody reviews. Generating it
// from the contract keeps the reviewable statement in the source, and avoids
// --enforce-contract, which demands a contract on every loop-shaped construct
// in the function -- eleven of them for zlib's inflate_table.
typedef unsigned long size_t;

// Clauses become statements in source order, so a clause bounding an
// allocation size has to come before the allocation that reads it.
void zero(char *p, size_t n)
  pre (n > 0 && n < 64)
  pre (fresh(p, n))
  assigns (p[0 : n])
{ *p = 0; }

// CHECK:      void __contract_harness_zero(void) {
// CHECK-NEXT:   char *__contract_arg_0;
// CHECK-NEXT:   size_t __contract_arg_1;
// CHECK-NEXT:   __CPROVER_assume(__contract_arg_1 > 0);
// CHECK-NEXT:   __CPROVER_assume(__contract_arg_1 < 64);
// CHECK-NEXT:   __contract_arg_0 = __CPROVER_allocate(__contract_arg_1, 0);
// CHECK-NEXT:   zero(__contract_arg_0, __contract_arg_1);
// CHECK-NEXT: }

// Get that order wrong and the entry point allocates an unbounded object, so
// every property downstream holds vacuously or fails for the wrong reason.
// Silence would be the worst outcome here.
void zero_unbounded(char *p, size_t n)
  pre (fresh(p, n)) // expected-warning-re {{size of the object allocated for p depends on {{.*}}n{{.*}}, which no earlier 'pre' constrains}} expected-note {{move the clause bounding 'n' above this one}}
  pre (n > 0 && n < 64)
  assigns (p[0 : n])
{ *p = 0; }

// 'fresh' takes any lvalue, not just a parameter: a buffer the caller reaches
// through an out-parameter -- zlib's 'code **table' -- is otherwise
// inexpressible, and an assumption over a pointer that was never allocated is
// vacuously satisfiable, so the old spelling proved nothing.
struct ent { int op; };
int build(struct ent **table, unsigned *bits)
  pre (fresh(bits, sizeof(unsigned)))
  pre (*bits == 3)
  pre (fresh(table, sizeof(struct ent *)))
  pre (fresh(*table, (1u << 3) * sizeof(struct ent)))
  assigns ()
{ return (*table)[0].op + (int)*bits; }

// CHECK:      void __contract_harness_build(void) {
// CHECK-NEXT:   struct ent **__contract_arg_0;
// CHECK-NEXT:   unsigned int *__contract_arg_1;
// CHECK-NEXT:   __contract_arg_1 = __CPROVER_allocate(sizeof(unsigned int), 0);
// CHECK-NEXT:   __CPROVER_assume(*__contract_arg_1 == 3);
// CHECK-NEXT:   __contract_arg_0 = __CPROVER_allocate(sizeof(struct ent *), 0);
// CHECK-NEXT:   *__contract_arg_0 = __CPROVER_allocate((1U << 3) * sizeof(struct ent), 0);
// CHECK-NEXT:   build(__contract_arg_0, __contract_arg_1);
// CHECK-NEXT: }

// A declaration with no body gets no entry point: there is nothing to call.
void declared_only(char *p, size_t n) pre (fresh(p, n));
// The rewriter echoes this file, so any literal spelling of the name matches
// the directive's own text. {{ }} is a regex, which breaks the literal.
// CHECK-NOT: void __contract_harness_dec{{l}}ared_only

void declared_then_defined(char *p, size_t n)
  pre (n < 16)
  pre (fresh(p, n))
  assigns (p[0 : n]);
void declared_then_defined(char *p, size_t n) { *p = 1; }

// CHECK:      void __contract_harness_declared_then_defined(void) {
// CHECK-NEXT:   char *__contract_arg_0;
// CHECK-NEXT:   size_t __contract_arg_1;
// CHECK-NEXT:   __CPROVER_assume(__contract_arg_1 < 16);
// CHECK-NEXT:   __contract_arg_0 = __CPROVER_allocate(__contract_arg_1, 0);
// CHECK-NEXT:   declared_then_defined(__contract_arg_0, __contract_arg_1);
// CHECK-NEXT: }

void callback(void (*cb)(int), int value)
  pre (value == 1)
  assigns ();
void callback(void (*cb)(int), int value) { cb(value); }

// CHECK:      void __contract_harness_callback(void) {
// CHECK-NEXT:   void (*__contract_arg_0)(int);
// CHECK-NEXT:   int __contract_arg_1;
// CHECK-NEXT:   __CPROVER_assume(__contract_arg_1 == 1);
// CHECK-NEXT:   callback(__contract_arg_0, __contract_arg_1);
// CHECK-NEXT: }

void combined(char *p, size_t n)
  pre (n < 8 && fresh(p, n))
  assigns (p[0 : n])
{ *p = 0; }

// CHECK:      void __contract_harness_combined(void) {
// CHECK-NEXT:   char *__contract_arg_0;
// CHECK-NEXT:   size_t __contract_arg_1;
// CHECK-NEXT:   __CPROVER_assume(__contract_arg_1 < 8);
// CHECK-NEXT:   __contract_arg_0 = __CPROVER_allocate(__contract_arg_1, 0);
// CHECK-NEXT:   combined(__contract_arg_0, __contract_arg_1);
// CHECK-NEXT: }

void duplicate_fresh(char *p, size_t n)
  pre (n > 0 && n < 8)
  pre (fresh(p, n))
  pre (fresh(p, n))
  assigns (p[0 : n])
{ *p = 0; }

// CHECK:      void __contract_harness_duplicate_fresh(void) {
// CHECK:      __contract_arg_0 = __CPROVER_allocate(__contract_arg_1, 0);
// CHECK-NOT:  __contract_arg_0 = __CPROVER_allocate(__contract_arg_1, 0);
// CHECK:      duplicate_fresh(__contract_arg_0, __contract_arg_1);

void shadows_name(int shadows_name)
  pre (shadows_name > 0)
  assigns ()
{}

// CHECK:      void __contract_harness_shadows_name(void) {
// CHECK-NEXT:   int __contract_arg_0;
// CHECK-NEXT:   __CPROVER_assume(__contract_arg_0 > 0);
// CHECK-NEXT:   shadows_name(__contract_arg_0);

void __contract_harness_renamed(void) {}
void renamed(int value) pre (value > 0) assigns () {}

// CHECK: void __contract_harness_renamed_1(void) {
