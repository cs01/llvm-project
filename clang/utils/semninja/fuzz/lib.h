// Fixture for fuzz.ts: one header with the C++ features a header edit can
// reach a translation unit through. Each tu_*.cpp uses a different slice.
#pragma once
#include <cassert>

#define SCALE 3
#define UNUSED_MACRO 1

namespace lib {
constexpr int Base = 1;
constexpr int Derived = Base + 1;

inline int twice(int x) {
  assert(x >= 0);
  return 2 * x;
}
inline int unused(int x) { return x + 7; }

int overloaded(int);
inline int overloaded(long x) { return (int)x; }

struct Point {
  int x, y;
  int sum() const { return x + y; }
  int scaled() const { return sum() * SCALE; }
};

struct Shape {
  virtual ~Shape() {}
  virtual int area() const { return 0; }
};
struct Box : Shape {
  int w = 1, h = 1;
  int area() const override { return w * h; }
};

template <typename T> struct Holder {
  T value;
  T get() const { return value; }
};
template <typename T> T identity(T v) { return v; }

enum Color { Red, Green };
using Count = int;
inline int useCount(Count c) { return c + 1; }

struct Opaque;
Opaque *makeOpaque();

namespace adl {
struct Tag {};
inline int find(Tag) { return 1; }
} // namespace adl
template <typename T> int callFind(T t) { return find(t); }
} // namespace lib
