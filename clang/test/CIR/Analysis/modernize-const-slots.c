// In C, functions stored into a callback typedef through designated
// initializers are slot members too: one that writes keeps the typedef, and
// with it every other function stored there.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const -fcir-modernize-export=%t.json %s -o /dev/null
// RUN: FileCheck --input-file=%t.json %s

struct Node { int value; };
typedef int (*FuseFn)(struct Node *n);
struct Desc { int id; FuseFn fusionFn; };
static int fuseWrite(struct Node *n) { n->value = 1; return 0; }
static int fuseRead(struct Node *n) { return n->value; }
static const struct Desc table[] = {
    { .id = 1, .fusionFn = fuseWrite },
    { .id = 2, .fusionFn = fuseRead },
};
int use(struct Node *n) { return table[0].fusionFn(n) + table[1].fusionFn(n); }

// Both functions are members of the FuseFn slot.
// CHECK:      "slotMembers": [
// CHECK:        "usr": "c:{{.*}}@F@fuseWrite",
// CHECK:        "usr": "c:{{.*}}@F@fuseRead",
