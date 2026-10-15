// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++20 -verify %s
//
// A pointer stored from a string literal is non-null: the literal decays to
// the address of its array.

void take(const char *_Nonnull s);
const char *maybe();

struct Str {
  Str(const char *_Nonnull s);
};
void takeStr(Str s);

void init_from_literal() {
  const char *p = "abc";
  take(p);
}

void ternary_of_literals(bool c) {
  // A suffix picked from two literals.
  const char *suffix = c ? ".on" : ".off";
  takeStr(suffix);
}

enum Reason { kA, kB, kC };
void assigned_only_literals(Reason r) {
  // A literal that is only ever replaced by other literals.
  const char *desc = "default";
  switch (r) {
  case kA:
    desc = "first";
    break;
  case kB:
    desc = "second";
    break;
  case kC:
    break;
  }
  take(desc);
}

void offset_into_literal() {
  const char *base = "prefix:value";
  const char *p = base + 7;
  take(p);
}

void guarded_ternary_with_null(bool c) {
  const char *p = c ? "x" : nullptr;
  if (c)
    take(p);
}

// Still reported.

void reassigned_from_nullable() {
  const char *p = "abc";
  p = maybe();
  take(p); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}

void ternary_with_null_arm(bool c) {
  const char *p = c ? "abc" : nullptr;
  take(p); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}

void literal_on_one_path(bool c) {
  const char *p = maybe();
  if (c)
    p = "abc";
  take(p); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}
