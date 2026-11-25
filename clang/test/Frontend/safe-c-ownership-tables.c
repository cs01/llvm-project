// Pointers in globals: only where nothing can change them.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN: -mllvm -safe-c-ownership=true -mllvm -safe-c-redeclared=false -verify %s
#define _Owned [[clang::annotate_type("safec_owned")]]
struct node { int v; struct node *_Owned _Nullable next; };
static const char *const fine[] = {"a"};
static const char *names[] = {"a"}; // expected-error {{put 'const' after the '*'}}
static struct node *const roots[] = {&(struct node){0}}; // expected-error {{shared by the whole program}}
typedef void (*nested)(void (*)(int));
void takes_nested(nested f) {} // expected-error {{this function pointer's type has no rule}}
