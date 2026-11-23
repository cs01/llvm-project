// '#pragma clang safe_c legacy' marks a header as code Safe C does not check:
// its declarations are trusted like a system header's. It is the allowed,
// searchable form of '#pragma clang system_header', which Safe C rejects, and
// it cannot apply to the file being compiled.
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -I %t -Rsafe-c-unsafe -verify %t/main.c
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -I %t -verify=self %t/self.c
// Control: the same headers without -fsafe-c are plain C.
// RUN: %clang_cc1 -fsyntax-only -I %t -verify=plain %t/main.c

//--- legacy.h
#pragma clang safe_c legacy
static inline int third(int *p) { return p[2]; }

//--- checked.h
static inline int third_checked(int *p) { return p[2]; }

//--- main.c
// expected-remark@legacy.h:1 {{legacy header: Safe C trusts its declarations and does not check its code}}
// expected-error@checked.h:1 {{array subscript on single pointer 'p' must use a constant index of 0}}
// expected-note@checked.h:1 {{__counted_by}}
// plain-no-diagnostics
#include "legacy.h"
#include "checked.h"
int use(int *p) { return third(p) + third_checked(p); }

//--- self.c
#pragma clang safe_c legacy // self-error {{the file being compiled is always checked}}
#pragma clang safe_c other  // self-error {{expected 'legacy' after '#pragma clang safe_c'}}
