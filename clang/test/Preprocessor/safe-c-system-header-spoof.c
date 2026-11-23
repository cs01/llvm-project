// Under -fsafe-c a user file cannot declare itself a system header: Safe C
// exempts system headers from its checks.
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -I %t -verify %t/main.c
// Without -fsafe-c both mechanisms work as usual.
// RUN: %clang_cc1 -fsyntax-only -I %t -verify=plain %t/plain.c

//--- user.h
#pragma clang system_header // expected-error {{'#pragma clang system_header' is not allowed under -fsafe-c}}
int *_Nullable user_get(void);
static inline int user_deref(int *_Nullable p) { return p ? *p : 0; }

//--- main.c
#include "user.h"
# 1 "fake.h" 3 // expected-error {{a line marker with the system-header flag is not allowed under -fsafe-c}}
int spoofed(int *_Nullable p) { return p ? *p : 0; }

//--- plain.c
// plain-no-diagnostics
# 1 "fake.h" 3
int fine(int *p) { return *p; }
