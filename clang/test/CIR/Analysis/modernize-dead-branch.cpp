// dead-branch (opt-in): an if with a constant condition loses the branch it
// never takes. Codegen already skips that branch, so the IR is unchanged. A
// condition that may depend on the build configuration is left alone.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=dead-branch -fno-caret-diagnostics %s -o /dev/null 2>&1 | FileCheck %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=dead-branch -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

int work(int);
static const bool kVerbose = false;
constexpr int kLevel = 2;
#define DEBUG_ON 0
static const bool kFromMacro = DEBUG_ON;

// CHECK: warning: this if in 'literal' has a constant condition
// FIXED:      {{^}}static int literal(int x) {
// FIXED-EMPTY:
// FIXED-EMPTY:
// FIXED-EMPTY:
// FIXED-NEXT:   return work(x);
// FIXED-NEXT: }
static int literal(int x) {
  if (0) {
    x = work(x + 1);
  }
  return work(x);
}

// A named constant is a knob: reported, not deleted.
// CHECK-NOT: in 'flagged'
// FIXED:      {{^}}static int flagged(int x) {
// FIXED-NEXT:   if (kLevel > 1 && !kVerbose) {
static int flagged(int x) {
  if (kLevel > 1 && !kVerbose) {
    x = 2;
  } else {
    x = work(x);
  }
  return x;
}

// CHECK-NOT: in 'fromMacro'
static int fromMacro(int x) {
  if (DEBUG_ON) {
    x = work(x);
  }
  if (kFromMacro) {
    x = work(x);
  }
  return x;
}

// CHECK-NOT: in 'labelled'
static int labelled(int x) {
  if (0) {
  again:
    x = work(x);
  }
  if (x < 0)
    goto again;
  return x;
}

// CHECK-NOT: in 'byTarget'
static int byTarget(int x) {
  if (sizeof(long) > sizeof(int)) {
    x = work(x);
  }
  return x;
}

// The lines after an edit keep their numbers.
// FIXED: {{^}}static int line() { return __LINE__; }
static int line() { return __LINE__; }

// CHECK-NOT: in 'explained'
static int explained(int x) {
  if (false) {
    // Checks that the overload still compiles.
    x = work(x);
  }
  return x;
}

// CHECK-NOT: in 'notConstant'
static int notConstant(int x) {
  if (x > 1) {
    x = work(x);
  }
  return x;
}

int use(int x) {
  return literal(x) + flagged(x) + fromMacro(x) + labelled(x) + byTarget(x) +
         line() + explained(x) + notConstant(x);
}
