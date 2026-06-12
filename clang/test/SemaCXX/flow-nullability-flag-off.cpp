// Flag-off no-op coverage. Without -fflow-sensitive-nullability and without
// -fnullability-default, representative flow-nullability test cases must
// compile exactly as upstream clang does (the feature is off by default).
//
// RUN: %clang_cc1 -fsyntax-only -std=c++11 -verify %s
// RUN: %clang_cc1 -fsyntax-only -std=c++17 -verify %s

struct Node {
  int value;
  Node *_Nullable next;
};

Node *_Nullable getNode();
int *_Nullable getInt();

// With -fflow-sensitive-nullability these all warn (nullable dereference);
// with the feature off they must be silent.
int derefArrow(Node *_Nullable p) { return p->value; }
int derefChain(Node *_Nullable p) { return p->next->value; }
int derefStar(int *_Nullable p) { return *p; }
int derefSubscript(int *_Nullable p) { return p[3]; }
int derefCallResult() { return getNode()->value; }

int *_Nonnull nullInitNonnull() {
  int *_Nonnull q = nullptr; // flow-on: warn_null_init_nonnull; off: silent
  return q;
}

void assignNullToNonnull(Node *_Nonnull n) {
  n = nullptr; // flow-on: nonnull-assign warning; off: silent
  (void)n;
}

#pragma clang assume_nonnull begin
int derefInPragmaRegion(int *_Nullable p) { return *p; }
#pragma clang assume_nonnull end

template <typename T>
struct SmartPtr {
  T *_Nullable ptr;
  T *_Nullable get() const;
  T &operator*() const;
  T *_Nullable operator->() const;
  explicit operator bool() const;
};

int smartDeref(SmartPtr<Node> &p) { return p->value; }

// Conversion operators must keep upstream behavior with the flags off.
// Outside a pragma region nothing fires (-Wnullability-completeness never
// fires in the main file upstream).
struct ConvPlain {
  operator int *();
};

// Inside an assume_nonnull region upstream infers _Nonnull on the member
// declarator's return type but not on the separately-processed
// conversion-type-id, so the two types mismatch and upstream errors.
// Reproducing that error proves the fork's conversion-operator exclusion is
// inactive when default-injection mode is off (bug-for-bug upstream parity);
// the previously ungated exclusion silenced it.
#pragma clang assume_nonnull begin
struct ConvInferred {
  operator int *(); // expected-error{{cannot specify any part of a return type in the declaration of a conversion function; put the complete type after 'operator'}}
};
#pragma clang assume_nonnull end
