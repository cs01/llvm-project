#include <stdio.h>
typedef void (*fn)(void);
fn table[4];
struct Ops { void (*run)(int); };
int main(void) { struct Ops o = {0}; o.run(1); table[1](); return 0; }
