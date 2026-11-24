// glibc's headers carry no bounds annotations; a literal format was then not
// recognized as one, so every printf was an error (Linux CI, 2026-11-24).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h \
// RUN:   -isystem %S/Inputs -Wformat -Wformat-nonliteral -Werror=format -Werror=format-nonliteral -verify %s
#include <safe-c-unannotated-libc.h>
int ok(int n) { return printf("%d\n", n); }
int wrong(int n) { return printf("%s\n", n); } // expected-error {{format specifies type 'char *'}}
int var(const char *f) { return printf(f, 1); } // expected-error {{format string is not a string literal}}
