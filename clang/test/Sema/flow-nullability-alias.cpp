// Test alias tracking: narrowing propagates between pointer aliases.
// When y = x, checking y for null also narrows x (and vice versa).
//
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -std=c++17 -Wno-unused-value %s -verify

// --- Basic bidirectional alias narrowing ---

void test_check_alias_narrow_original(int *_Nullable x) {
  int *y = x;
  if (y) {
    (void)*x; // no warning — y aliases x, y is checked
  }
  (void)*x; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_check_original_narrow_alias(int *_Nullable x) {
  int *y = x;
  if (x) {
    (void)*y; // no warning — x is checked, y aliases x
  }
  (void)*y; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// --- Alias invalidation on reassignment ---

void test_alias_invalidated_by_reassignment(int *_Nullable x, int *_Nullable q) {
  int *y = x;
  y = q;       // y no longer aliases x
  if (y) {
    (void)*x; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
  }
}

void test_alias_invalidated_by_source_reassignment(int *_Nullable x, int *_Nullable q) {
  int *y = x;
  x = q;       // x reassigned — alias y → x is stale
  if (y) {
    (void)*x; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
  }
}

// --- Multiple aliases to the same target ---

void test_multiple_aliases(int *_Nullable x) {
  int *y = x;
  int *z = x;
  if (z) {
    (void)*x; // no warning — z aliases x, z is checked
    (void)*y; // no warning — y also aliases x, x is narrowed
  }
}

// --- Alias chain resolution (canonical target) ---

void test_alias_chain(int *_Nullable x) {
  int *y = x;
  int *z = y;  // z → canonical(y) → x
  if (z) {
    (void)*x; // no warning — z ultimately aliases x
    (void)*y; // no warning — y aliases x too
  }
}

// --- Alias invalidation by increment ---

void test_alias_invalidated_by_increment(int *_Nullable x) {
  int *y = x;
  x++;         // x changed — alias is stale // expected-warning{{pointer arithmetic on nullable pointer}} expected-note{{add a null check before performing arithmetic}}
  if (y) {
    (void)*x; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
  }
}

// --- Alias with early return pattern ---

void test_alias_early_return(int *_Nullable x) {
  int *y = x;
  if (!y)
    return;
  (void)*x; // no warning — early return means y (and thus x) is non-null
}

// --- Alias does not propagate through non-simple expressions ---

int *_Nullable get_ptr();

void test_no_alias_for_call_result(int *_Nullable x) {
  int *y = get_ptr(); // y does NOT alias x
  if (y) {
    (void)*x; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
  }
}

// --- Alias with && short-circuit ---

void test_alias_and_shortcircuit(int *_Nullable a, int *_Nullable b) {
  int *x = a;
  int *y = b;
  if (x && y) {
    (void)*a; // no warning — x aliases a, x is checked
    (void)*b; // no warning — y aliases b, y is checked
  }
}

// --- Alias with negated && (!(p && q) return) ---

void test_alias_negated_and(int *_Nullable a, int *_Nullable b) {
  int *x = a;
  int *y = b;
  if (!(x && y))
    return;
  (void)*a; // no warning
  (void)*b; // no warning
}

// --- Both alias and bool guard ---

void test_alias_with_bool_guard(int *_Nullable x) {
  int *y = x;
  bool ok = (y != nullptr);
  if (ok) {
    (void)*x; // no warning — bool guard resolves y, alias propagates to x
  }
}
