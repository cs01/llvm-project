#include "contracts.h"

int impure(int);
struct Big { int a[16]; };

// 1. a 'c_post' naming a by-value parameter, without c_old()
int a(int n) c_post (r: r <= n);

// 2. c_old() in a 'c_pre'
int b(int n) c_pre (c_old(n) > 0);

// 3. c_old() of a non-scalar
int c(struct Big s) c_post (r: c_old(s).a[0] == 0);

// 4. an impure predicate
int d(int n) c_pre (impure(n) > 0);

// 5. mismatched contracts on a redeclaration
int e(int n) c_pre (n > 0);
int e(int n) c_pre (n >= 0) { return n; }

// 6. a contract on something that isn't a function
int (*fp)(int n) c_pre (n > 0);

// 7. a 'c_assigns' target that names no location
int g(int *p) c_assigns (p + 1);

// 8. a macro that would silently eat the grammar
#define pre(x)
