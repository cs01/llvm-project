// RUN: %clang_cc1 -fsyntax-only -internal-isystem %S/../../lib/Headers -fc-contracts -verify %s
// RUN: %clang_cc1 -fsyntax-only -internal-isystem %S/../../lib/Headers -std=c89 -Wall -Wno-comment -verify %s

// expected-no-diagnostics

/* Include order must not decide the language. In a real project some other
 * header pulls c_contracts.h in first, without asking for the unprefixed
 * spelling; the opt-in then arrives on a second include. While the aliases sat
 * inside the main include guard that second include expanded to nothing, and
 * every unprefixed clause failed with "call to undeclared function 'range'".
 * Hit on zstd, where bitstream.h includes the header before zstd_internal.h
 * asks for the unprefixed form. */
#include <c_contracts.h>

#define C_CONTRACTS_NO_PREFIX
#include <c_contracts.h>

void late_optin(char *p, unsigned n)
    pre(p != 0)
    pre(writable(p, n))
    assigns(range(p, 0, n));
