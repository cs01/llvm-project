// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++20 -verify %s
//
// A const pointer whose initializer, seen in this TU, is a constant non-null
// address (a string literal, or a named object that is not weak) is non-null
// wherever it is read.

void take(const char *_Nonnull s);
const char *maybe();

struct Str {
  Str(const char *_Nonnull s);
  Str &operator=(const char *_Nonnull s);
};

// A namespace-scope constant.
const char *const kTail = "tail";
void namespace_constant() {
  take(kTail);
}
char dereference_constant() {
  return *kTail;
}
void copy_constant() {
  const char *copy = kTail;
  take(copy);
}

// A static data member defined out of line, after its first use.
struct Settings {
  static const char *const kDefault;
  void init();
  Str value_ = "";
};
void Settings::init() {
  value_ = kDefault;
}
const char *const Settings::kDefault = "default";

struct QualifiedSettings {
  static const char *const kDefault;
  void use() { take(this->kDefault); }
};
const char *const QualifiedSettings::kDefault = "qualified";

void object_qualified_constants(QualifiedSettings &settings,
                                QualifiedSettings *_Nonnull settings_ptr) {
  take(settings.kDefault);
  take(settings_ptr->kDefault);
  const char *copy = settings.kDefault;
  take(copy);
}

const char *_Nonnull
object_qualified_return(QualifiedSettings &settings) {
  return settings.kDefault;
}

constexpr const char *kConstexpr = "abc";
constexpr const char *kAlias = kConstexpr;
inline constexpr char kArray[] = "http";
const char *const kToArray = kArray;
void constexpr_constants() {
  take(kConstexpr);
  take(kAlias);
  take(kToArray);
  static const char *const kStatic = "static";
  take(kStatic);
}

// Still reported.

extern const char *const kExtern;
const char *gMutable = "abc";
const char *const kNull = nullptr;
extern __attribute__((weak)) const char *const kWeak = "abc";
const char *const kDynamic = maybe();
const char *const kDynamicLiterals = maybe() ? "a" : "b";
void not_provable() {
  take(kExtern);          // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(gMutable);         // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kNull);            // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-warning {{null passed to a callee that requires a non-null argument}} expected-note {{add a null check before the call}}
  take(kWeak);            // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kDynamic);         // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kDynamicLiterals); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}
