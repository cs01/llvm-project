#include "contracts.h"

int impure(int);
struct Big { int a[16]; };

// 1. a post naming a by-value parameter, without old()
int a(int n) post (r: r <= n);

// 2. old() in a pre
int b(int n) pre (old(n) > 0);

// 3. old() of a non-scalar
int c(struct Big s) post (r: old(s).a[0] == 0);

// 4. an impure predicate
int d(int n) pre (impure(n) > 0);

// 5. contracts restated on a redeclaration
int e(int n) pre (n > 0);
int e(int n) pre (n > 0) { return n; }

// 6. a contract on something that isn't a function
int (*fp)(int n) pre (n > 0);

// 7. writes, not implemented yet
int g(int *p) writes (p);

// 8. a macro that would silently eat the grammar
#define pre(x)
