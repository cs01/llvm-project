// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c11 -verify %s
//
// In C every global has a constant initializer, so the constant's value alone
// decides: it must be the address of a string literal or of an object that is
// not weak.

void take(const char *_Nonnull s);

static const char *const kName = "name";
static const char kBuffer[] = "buffer";
const char *const kToBuffer = kBuffer;
const char *const kOffset = kBuffer + 2;

void quiet(void) {
  take(kName);
  take(kToBuffer);
  take(kOffset);
  static const char *const kLocal = "local";
  take(kLocal);
}
char dereference(void) { return *kName; }

extern const char *const kExtern;
const char *const kInteger = (const char *)16;
const char *const kZero = 0;
const char *gMutable = "mutable";
const char *const kTentative;
extern __attribute__((weak)) const char kWeakBuffer[];
const char *const kToWeak = kWeakBuffer;

void reported(void) {
  take(kExtern);    // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kInteger);   // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kZero);      // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-warning {{null passed to a callee that requires a non-null argument}} expected-note {{add a null check before the call}}
  take(gMutable);   // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kTentative); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kToWeak);    // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}
