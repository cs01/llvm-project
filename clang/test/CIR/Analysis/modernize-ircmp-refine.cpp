// cir-modernize-ircmp --refine: a view with a checked operator[] is equal to
// the raw pointer and length it replaces once the check folds away, which it
// does only where the code already guards the access.

// RUN: rm -rf %t && split-file %s %t && cd %t
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -emit-llvm orig.cpp -o orig.ll
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -emit-llvm view.cpp -o view.ll
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -emit-llvm oob.cpp -o oob.ll
// RUN: cir-modernize-ircmp --refine orig.ll view.ll
// RUN: not cir-modernize-ircmp --refine orig.ll oob.ll | FileCheck %s
// CHECK: a check the rewrite added did not fold away: 1 more traps
// A guard's destructor unlocks where the pair did; inlining also changes the
// order declarations are first needed in, which pairing must not see.
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -emit-llvm pair.cpp -o pair.ll
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -O1 -disable-llvm-passes -emit-llvm guard.cpp -o guard.ll
// RUN: cir-modernize-ircmp --refine pair.ll guard.ll
// Without the normalization the calls to the view differ.
// RUN: not cir-modernize-ircmp orig.ll view.ll

//--- view.h
struct View {
  const int *p;
  unsigned long n;
  unsigned long size() const { return n; }
  const int &operator[](unsigned long i) const {
    if (i >= n)
      __builtin_trap();
    return p[i];
  }
};

//--- orig.cpp
static int sum(const int *p, unsigned long n) {
  int s = 0;
  for (unsigned long i = 0; i < n; ++i)
    s += p[i];
  return s;
}
int entry(const int *a, unsigned long n) { return sum(a, n); }

//--- view.cpp
#include "view.h"
static int sum(View v) {
  int s = 0;
  for (unsigned long i = 0; i < v.size(); ++i)
    s += v[i];
  return s;
}
int entry(const int *a, unsigned long n) { return sum({a, n}); }

//--- oob.cpp
#include "view.h"
static int sum(View v) {
  int s = 0;
  for (unsigned long i = 0; i <= v.size(); ++i)
    s += v[i];
  return s;
}
int entry(const int *a, unsigned long n) { return sum({a, n}); }

//--- mutex.h
struct Mutex {
  void lock();
  void unlock();
};
void work(int &);

//--- pair.cpp
#include "mutex.h"
void f(Mutex &m, int &x) {
  m.lock();
  work(x);
  m.unlock();
}

//--- guard.cpp
#include "mutex.h"
struct Guard {
  Mutex &m;
  Guard(Mutex &m) : m(m) { m.lock(); }
  ~Guard() { m.unlock(); }
};
void f(Mutex &m, int &x) {
  Guard g(m);
  work(x);
}
