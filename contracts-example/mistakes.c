#include "contracts.h"

int impure(int);
struct Big { int a[16]; };

// 1. an 'ensures' naming a by-value parameter, without old()
int a(int n) ensures (r: r <= n);

// 2. old() in a 'requires'
int b(int n) requires (old(n) > 0);

// 3. old() of a non-scalar
int c(struct Big s) ensures (r: old(s).a[0] == 0);

// 4. an impure predicate
int d(int n) requires (impure(n) > 0);

// 5. contracts restated on a redeclaration
int e(int n) requires (n > 0);
int e(int n) requires (n > 0) { return n; }

// 6. a contract on something that isn't a function
int (*fp)(int n) requires (n > 0);

// 7. an 'assigns' target that names no location
int g(int *p) assigns (p + 1);

// 8. a macro that would silently eat the grammar
#define requires(x)
