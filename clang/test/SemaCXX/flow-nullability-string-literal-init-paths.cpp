// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++20 -verify %s
//
// Every store and use path that classifies a stored value: assignment over a
// nullable value, dereference, indexing, a member store, an explicit cast,
// other literal kinds, the all-returns-nonnull summary, and switches.

void take(const char *_Nonnull s);
void takeW(const wchar_t *_Nonnull s);
const char *maybe();

void overwritten_by_literal() {
  const char *p = maybe();
  p = "abc";
  take(p);
}

char deref_literal_local() {
  const char *p = "abc";
  return *p;
}

char index_literal_local() {
  const char *p = "abc";
  return p[1];
}

struct Rec {
  const char *name;
};
void member_assigned_literal() {
  Rec r;
  r.name = "abc";
  take(r.name);
}

void cast_literal() {
  const char *p = maybe();
  p = (const char *)"abc";
  take(p);
}

void other_literal_kinds() {
  const wchar_t *w = L"abc";
  takeW(w);
  const char8_t *u = u8"abc";
  (void)*u;
}

// Every return is a pointer set only from literals, so callers see non-null.
const char *name_of(int k) {
  const char *s = "unknown";
  if (k == 1)
    s = "one";
  return s;
}
void call_name_of(int k) { take(name_of(k)); }

// A switch over every enumerator sets the pointer on every path: with a
// literal now, as it already did with &x.
enum class Trig { kA, kB, kC };
char buf[4];
void every_enumerator_addr(Trig t) {
  const char *p = nullptr;
  switch (t) {
  case Trig::kA: p = &buf[0]; break;
  case Trig::kB: p = &buf[1]; break;
  case Trig::kC: p = &buf[2]; break;
  }
  take(p);
}
void every_enumerator_literal(Trig t) {
  const char *p = nullptr;
  switch (t) {
  case Trig::kA: p = "a"; break;
  case Trig::kB: p = "b"; break;
  case Trig::kC: p = "c"; break;
  }
  take(p);
}

// Still reported.

void default_keeps_null(Trig t) {
  const char *p = nullptr;
  switch (t) {
  case Trig::kA: p = "a"; break;
  case Trig::kB: p = "b"; break;
  default: break;
  }
  take(p); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}

void int_switch_keeps_null(int t) {
  const char *p = nullptr;
  switch (t) {
  case 0: p = "a"; break;
  case 1: p = "b"; break;
  }
  take(p); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}

void literal_then_null(bool c) {
  const char *p = "abc";
  if (c)
    p = nullptr;
  take(p); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}

char deref_after_nullable_store() {
  const char *p = "abc";
  p = maybe();
  return *p; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void member_assigned_nullable() {
  Rec r;
  r.name = maybe();
  take(r.name); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}

// A static local is initialized once, so a later call can see what an earlier
// call stored.
const char *cached_name() {
  static const char *cached = "";
  if (!*cached) // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
    cached = maybe();
  return cached;
}

// One return is not a literal, so the summary does not apply.
const char *name_or_maybe(int k) {
  if (k == 1)
    return "one";
  return maybe();
}
void call_name_or_maybe(int k) {
  take(name_or_maybe(k)); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}
