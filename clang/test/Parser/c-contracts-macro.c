// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify=expected,cmdline -Dpost=oops %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts -Wno-c-contracts -verify=quiet %s
// RUN: %clang_cc1 -fsyntax-only -verify=quiet %s

// A macro named after a clause keyword rewrites every contract that follows it,
// and the rewrite is invisible because the parser only ever sees the expansion.
#define pre(x) // expected-warning {{macro named 'pre' hides the contract clause keyword of the same name}}
#define assigns // expected-warning {{macro named 'assigns' hides the contract clause keyword of the same name}}

// An unrelated macro whose name merely starts the same is not a collision.
#define requires_review(x) x

// A macro from -D is caught too: it reaches the preprocessor through the
// predefines buffer, which is lexed once parsing starts.
// cmdline-warning@* {{macro named 'post' hides the contract clause keyword of the same name}}

// quiet-no-diagnostics
