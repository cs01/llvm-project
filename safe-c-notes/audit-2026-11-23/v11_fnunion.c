#include <stdio.h>
union U { long i; void (*f)(void); };
struct A { long x; }; struct B { void (*f)(void); };
int main(int argc, char **argv) {
  union U u; u.i = 0x41414140; u.f();
  struct A a = { 0x41414140 }; struct B *b = (struct B *)&a; b->f();
  return 0;
}
