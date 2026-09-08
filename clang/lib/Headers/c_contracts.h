/*===-- c_contracts.h - C contracts portable annotation layer -------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===
 *
 * Annotations that vanish under a compiler that does not understand them.
 *
 * This header is the annotation language. The contract grammar it expands to is
 * one of three targets, chosen at include time:
 *
 *   contract-aware front end   __has_feature(c_contracts): pre/post/assigns
 *   CBMC directly              -DC_CONTRACTS_CPROVER: __CPROVER_requires etc.
 *   everything else            nothing at all
 *
 * Annotated source stays buildable by GCC, MSVC, tcc and stock clang, at any
 * standard level from C89 on, with no second code path -- every target produces
 * the same declaration.
 *
 * This file is self-contained and intended to be vendored: copy it into a
 * project rather than depending on a particular compiler shipping it. It is
 * written in C89 with no includes so that it cannot constrain what includes it.
 *
 * No variadic macros are used, so -std=c89 -pedantic stays quiet. c_locations
 * combines two frame locations and nests when a frame needs more.
 *
 *===----------------------------------------------------------------------===
 */

#ifndef __C_CONTRACTS_H
#define __C_CONTRACTS_H

#ifdef __has_feature
#if __has_feature(c_contracts)
#define C_CONTRACTS 1
#endif
#endif

#ifndef C_CONTRACTS
#define C_CONTRACTS 0
#endif

/* Define C_CONTRACTS_CPROVER before including this header to target CBMC's own
 * front end directly, with no contract-aware compiler in the pipeline:
 *
 *   goto-cc -DC_CONTRACTS_CPROVER -o f.goto f.c
 *   goto-instrument --enforce-contract f f.goto f-chk.goto
 *   cbmc --function f --pointer-check --bounds-check f-chk.goto
 *
 * The same annotated source then reaches a verifier through three independent
 * paths, which is the point of putting the language in a header.
 *
 * Everything in the language survives this mode, but only through the c_ names.
 * The bare spellings a contract-aware front end also accepts -- readable, old,
 * result, a p[lo : hi] range, forall (i : lo, hi) P -- are grammar rather than
 * macros, and nothing discards them here. Write c_readable, c_same_object,
 * c_pointer_offset, c_old, c_result, c_range and c_forall in source that has to
 * reach CBMC directly.
 */
#ifdef C_CONTRACTS_CPROVER
#undef C_CONTRACTS
#define C_CONTRACTS 0

#define c_reads(P, N)                                                          \
  __CPROVER_requires((P) != 0) __CPROVER_requires(__CPROVER_r_ok((P), (N)))
#define c_writes(P, N)                                                         \
  __CPROVER_requires((P) != 0) __CPROVER_requires(__CPROVER_w_ok((P), (N)))     \
      __CPROVER_assigns(__CPROVER_object_upto(((char *)(P)), (N)))
#define c_reads_n(P, N)                                                        \
  __CPROVER_requires((P) != 0)                                                 \
      __CPROVER_requires(__CPROVER_r_ok((P), (N) * sizeof(*(P))))
#define c_writes_n(P, N)                                                       \
  __CPROVER_requires((P) != 0)                                                 \
      __CPROVER_requires(__CPROVER_w_ok((P), (N) * sizeof(*(P))))              \
          __CPROVER_assigns(__CPROVER_object_upto((P), (N) * sizeof(*(P))))

#define c_writes_nothing __CPROVER_assigns()
#define c_returns(P) __CPROVER_ensures(P)

#define c_pre(P) __CPROVER_requires(P)
#define c_post(P) __CPROVER_ensures(P)
#define c_assigns(L) __CPROVER_assigns(L)
#define c_locations(A, B) A, B
#define c_invariant(P) __CPROVER_loop_invariant(P)
#define c_decreases(M) __CPROVER_decreases(M)

#define c_readable(P, N) __CPROVER_r_ok((P), (N))
#define c_writable(P, N) __CPROVER_w_ok((P), (N))
#define c_fresh(P, N) __CPROVER_is_fresh((P), (N))
#define c_same_object(P, Q) __CPROVER_same_object((P), (Q))
#define c_pointer_offset(P) __CPROVER_POINTER_OFFSET(P)
#define c_old(E) __CPROVER_old(E)
#define c_result __CPROVER_return_value
#define c_range(P, LO, HI)                                                     \
  __CPROVER_object_upto((P) + (LO), ((HI) - (LO)) * sizeof(*(P)))
#define c_forall(I, LO, HI, P)                                                 \
  __CPROVER_forall { unsigned long I; ((I) >= (LO) && (I) < (HI)) ==> (P) }

#elif C_CONTRACTS

/* Roles: what the function does to a buffer. A role is the spelling to reach
 * for; the clauses below are what it lowers to. c_writes covers both the
 * caller's obligation to supply the memory and the promise that nothing outside
 * it changes, because a function that writes a buffer always means both.
 *
 * There are two roles, not three. A function that reads a buffer and then
 * writes it carries both, and the difference that matters is stated by their
 * combination rather than by a third name: c_reads is what obliges the caller
 * to have initialized the memory. memset only writes; buf[i] *= 2 does both.
 *
 * A count is in BYTES, matching memcpy and every C interface that pairs a
 * void * with a size. The _n forms count ELEMENTS of a typed pointer.
 */
#define c_reads(P, N) pre((P) != 0) pre(readable((P), (N)))
#define c_writes(P, N)                                                         \
  pre((P) != 0) pre(writable((P), (N))) assigns(((char *)(P))[0 : (N)])

#define c_reads_n(P, N) pre((P) != 0) pre(readable((P), (N) * sizeof(*(P))))
#define c_writes_n(P, N)                                                       \
  pre((P) != 0) pre(writable((P), (N) * sizeof(*(P)))) assigns((P)[0 : (N)])

/* The function writes nothing a caller can observe. An annotation with no write
 * role and no c_assigns makes no claim about the frame at all, so a pure reader
 * needs this to say so.
 */
#define c_writes_nothing assigns()

/* The result, under a fixed name, so nothing has to be bound by hand. */
#define c_returns(P) post(result : P)

/* Prefixed spellings of the predicates that appear inside a clause. Under a
 * contract-aware front end these are the keywords and intrinsics themselves, so
 * the short names work too; the c_ forms are what a translation unit targeting
 * CBMC directly has to use, since nothing discards them there.
 */
#define c_readable(P, N) readable((P), (N))
#define c_writable(P, N) writable((P), (N))
#define c_fresh(P, N) fresh((P), (N))
#define c_same_object(P, Q) same_object((P), (Q))
#define c_pointer_offset(P) pointer_offset(P)
#define c_old(E) old(E)
#define c_result result
#define c_range(P, LO, HI) (P)[(LO) : (HI)]
#define c_forall(I, LO, HI, P) forall(I : LO, HI)(P)

/* The primitive layer. Reach for these when a role cannot say it: a global in
 * the frame, a partial write, a relation between two parameters.
 */
#define c_pre(P) pre(P)
#define c_post(P) post(P)
#define c_assigns(L) assigns(L)
#define c_locations(A, B) A, B
#define c_invariant(P) loop_invariant(P)
#define c_decreases(M) decreases(M)

#else

#define c_reads(P, N)
#define c_writes(P, N)
#define c_reads_n(P, N)
#define c_writes_n(P, N)
#define c_writes_nothing
#define c_returns(P)
#define c_forall(I, LO, HI, P)
#define c_pre(P)
#define c_post(P)
#define c_assigns(L)
#define c_invariant(P)
#define c_decreases(M)

#endif

/* Unprefixed spelling, opt-in. Under a contract-aware front end the keywords
 * already exist, so nothing is defined and no macro shadows them; elsewhere the
 * same five names discard their argument. Off by default because a project may
 * already use one of these identifiers.
 */
#ifdef C_CONTRACTS_NO_PREFIX
#if !C_CONTRACTS
#define pre(P)
#define post(P)
#define assigns(L)
#define loop_invariant(P)
#define decreases(M)
#endif
#endif

#ifdef __cplusplus
extern "C" void __contract_violation(const char *predicate, const char *file,
                                     unsigned line, const char *function);
#else
void __contract_violation(const char *predicate, const char *file,
                          unsigned line, const char *function);
#endif

#endif
