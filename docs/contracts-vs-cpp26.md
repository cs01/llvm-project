# Contracts here vs. C++26 contracts

C++26 standardised contracts in [P2900](https://open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2900r14.pdf),
and GCC 16 implements them. This is not a competing proposal; it targets a
different language and a different job. P2900 is a *runtime checking* facility.
This is a *proof* vocabulary that also checks at run time.

|                                  | C++26 (P2900)                          | this branch                                      |
| -------------------------------- | -------------------------------------- | ------------------------------------------------ |
| language                         | C++                                    | **C** (rejected under `-x c++`)                   |
| status                           | **standardised**, ships in GCC 16      | a fork, one implementation                        |
| clauses                          | `pre`, `post`, `contract_assert`       | `pre`, `post`, `old`, `assigns`, `loop_invariant`, `decreases` |
| entry values in `post`           | none; a parameter named in `post` must be `const` | `old(x)`                          |
| frame conditions                 | —                                      | `assigns (dst[0 : n])`                            |
| loop reasoning                   | —                                      | `loop_invariant`, `decreases`                     |
| memory predicates                | —                                      | `readable`, `writable`, `fresh`, `same_object`, `pointer_offset` |
| when violations surface          | run time                               | compile time (caller dataflow), run time, or proof |
| static proof                     | out of scope                           | CBMC, entry point generated from the contract     |
| may the optimiser assume it?     | **no**, by design — even unchecked     | no (see below)                                    |

The four clauses C++26 does not have are not decoration: they are what a prover
needs. `pre`/`post` alone cannot say what a function may modify, cannot make a
loop tractable, and cannot describe the extent of a buffer. zlib's
`inflate_table` needs `fresh` on the target of an out-parameter — `code **table`
— and that sentence has no spelling in P2900.

**On the assumption question.** The contentious part of P2900 is that a contract
predicate may not be assumed by the optimiser, even under a checking semantic;
Daniel Lemire's example is a `pre` that the divisor is a power of two, which
GCC 16 checks and then still fails to use to strength-reduce `i % n` into
`i & (n - 1)`; `[[assume]]` gets the optimisation, the checked contract does
not. This branch behaves
identically and for a worse reason: `-fcontract-runtime-checks` emits a
compare-and-branch to `__contract_violation` and no `llvm.assume`, so the
optimiser learns nothing either way.

The objection driving P2900's rule is sound — an *unproven* predicate fed to the
optimiser is undefined behaviour waiting to happen. It does not apply to a
predicate CBMC has proved. Proved facts are precisely the ones that are safe to
assume, and a design whose contracts are only ever checked cannot produce them.
Nothing here exploits that yet; it is a consequence of the architecture worth
naming, not a feature.

**Where C++26 is ahead:** it is a standard with a specification, a conformance
story, and a shipping implementation, and its four evaluation semantics
(ignore / observe / enforce / quick-enforce) are a considered answer to how a
large codebase adopts checking incrementally. This branch has none of that. It
has a proof tier, which C++26 deliberately does not attempt.
