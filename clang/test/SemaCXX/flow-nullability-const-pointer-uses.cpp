// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++20 -verify %s
//
// Every kind of read treats a constant non-null pointer as non-null. The
// pointer object must be const and not volatile, constant-initialized, and
// hold the address of a string literal or of an object or function that is
// not weak.

void take(const char *_Nonnull s);
const char *maybe();

const char *const kTail = "tail";
constexpr const char *kConstexpr = "abc";

void arithmetic() { take(kTail + 1); }
char subscript() { return kTail[1]; }

struct Point {
  int x;
};
constexpr Point kOrigin{};
const Point *const kOriginPtr = &kOrigin;
int arrow() { return kOriginPtr->x; }

void callback();
void (*const kCallback)() = callback;
void function_pointer() { (*kCallback)(); }

thread_local const char *const kThreadLocal = "tls";
void thread_local_constant() { take(kThreadLocal); }

struct Names {
  static constexpr const char *kName = "name";
  static inline const char *const kInline = "inline";
  void use() {
    take(kName);
    take(kInline);
  }
};

void constexpr_local_in_lambda() {
  constexpr const char *kLocal = "local";
  [] { take(kLocal); }();
}

inline constexpr char kArray[] = "http";
const char *const kSecond = &kArray[1];
void array_element() { take(kSecond); }

const char *_Nonnull nonnull_return() { return kTail; }
void nonnull_local() {
  const char *_Nonnull local = kTail;
  take(local);
}
void ternary_of_constants(bool c) { take(c ? kTail : kConstexpr); }
void guarded_ternary(bool c) {
  const char *p = c ? kTail : nullptr;
  if (c)
    take(p);
}

template <typename T> struct Templated {
  static const char *const kName;
};
template <typename T> const char *const Templated<T>::kName = "templated";
void template_member() { take(Templated<int>::kName); }

const char *const kFoldedLiterals = true ? "a" : "b";
void folded_literals() { take(kFoldedLiterals); }

// A constant static member named through this, behind a cast and an offset.
template <class T> struct HexBase {
  static const char *const kDigits;
};
template <class T>
const char *const HexBase<T>::kDigits = "0123456789abcdef";
template <class T> struct Hex : HexBase<T> {
  char digit(int i) const {
    return *(reinterpret_cast<const char *>(this->kDigits) + i);
  }
};
char hex_digit() { return Hex<int>().digit(3); }

// Still reported.

const char *const volatile kVolatile = "abc";
extern __attribute__((weak)) const char kWeakArray[];
const char *const kToWeak = kWeakArray;
__attribute__((weak)) void weak_function();
void (*const kWeakFunction)() = weak_function;
const char *const kInteger = reinterpret_cast<const char *>(16);
constexpr bool kFlag = false;
const char *const kFoldedNull = kFlag ? "a" : nullptr;
// kTail is not usable in a constant expression, so this copy is initialized
// dynamically.
const char *const kCopy = kTail;
struct Mutable {
  static const char *kName;
};
const char *Mutable::kName = "x";
void not_provable() {
  take(kVolatile);      // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kToWeak);        // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  (*kWeakFunction)();   // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  take(kInteger);       // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(kFoldedNull);    // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-warning {{null passed to a callee that requires a non-null argument}} expected-note {{add a null check before the call}}
  take(kCopy);          // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  take(Mutable::kName); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
  static const char *const kLocalDynamic = maybe();
  take(kLocalDynamic);  // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}
void default_argument(const char *const p = "x") {
  take(p); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}

// A pointer dynamic_cast can fail even when its operand is a constant.
struct DynBase {
  virtual ~DynBase();
};
struct DynDerived : DynBase {
  int x;
};
DynDerived gDerived;
DynBase *const kDynBase = &gDerived;
int dynamic_cast_of_constant() {
  return dynamic_cast<DynDerived *>(kDynBase)->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// A static local is initialized once, so a later call can see what an earlier
// call stored.
const char *const kStart = "start";
const char *current(bool advance) {
  static const char *cur = kStart;
  const char *result = cur;
  if (advance)
    cur = nullptr;
  return result;
}
char use_current() {
  current(true);
  return *current(false); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
