// The error is where the pointer is used, not where its field is declared.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN:   -mllvm -safe-c-ownership=true -mllvm -safe-c-redeclared=false -verify %s
#define _Owned [[clang::annotate_type("safec_owned")]]
struct json { int kind; };
void json_delete(struct json *_Owned j);
int f(struct json *_Owned t) {
  json_delete(t);
  return t->kind; // expected-error {{is used after it was passed to 'json_delete' on line 8}}
}
