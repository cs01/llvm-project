// Performance regression guard for flow-sensitive nullability analysis.
// This file generates a large amount of work for the analysis and must
// compile within the default lit timeout. If the analysis has a complexity
// regression, this test will time out.
//
// Modeled after clang/test/Analysis/runtime-regression.c — the test passes
// if it finishes; there are no diagnostic expectations beyond that.
//
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

struct Node {
    int value;
    Node * _Nullable next;
    Node * _Nullable left;
    Node * _Nullable right;
};

Node * _Nullable getNode();
int getInt();

#pragma clang assume_nonnull begin

// --- Pattern 1: Many sequential null checks (tests linear scaling) ---
// 100 variables, each checked and used

#define CHECK_AND_USE(N) \
    { Node * _Nullable p##N = getNode(); if (p##N) p##N->value = N; }

void stress_sequential() {
    CHECK_AND_USE(0)  CHECK_AND_USE(1)  CHECK_AND_USE(2)  CHECK_AND_USE(3)
    CHECK_AND_USE(4)  CHECK_AND_USE(5)  CHECK_AND_USE(6)  CHECK_AND_USE(7)
    CHECK_AND_USE(8)  CHECK_AND_USE(9)  CHECK_AND_USE(10) CHECK_AND_USE(11)
    CHECK_AND_USE(12) CHECK_AND_USE(13) CHECK_AND_USE(14) CHECK_AND_USE(15)
    CHECK_AND_USE(16) CHECK_AND_USE(17) CHECK_AND_USE(18) CHECK_AND_USE(19)
    CHECK_AND_USE(20) CHECK_AND_USE(21) CHECK_AND_USE(22) CHECK_AND_USE(23)
    CHECK_AND_USE(24) CHECK_AND_USE(25) CHECK_AND_USE(26) CHECK_AND_USE(27)
    CHECK_AND_USE(28) CHECK_AND_USE(29) CHECK_AND_USE(30) CHECK_AND_USE(31)
    CHECK_AND_USE(32) CHECK_AND_USE(33) CHECK_AND_USE(34) CHECK_AND_USE(35)
    CHECK_AND_USE(36) CHECK_AND_USE(37) CHECK_AND_USE(38) CHECK_AND_USE(39)
    CHECK_AND_USE(40) CHECK_AND_USE(41) CHECK_AND_USE(42) CHECK_AND_USE(43)
    CHECK_AND_USE(44) CHECK_AND_USE(45) CHECK_AND_USE(46) CHECK_AND_USE(47)
    CHECK_AND_USE(48) CHECK_AND_USE(49) CHECK_AND_USE(50) CHECK_AND_USE(51)
    CHECK_AND_USE(52) CHECK_AND_USE(53) CHECK_AND_USE(54) CHECK_AND_USE(55)
    CHECK_AND_USE(56) CHECK_AND_USE(57) CHECK_AND_USE(58) CHECK_AND_USE(59)
    CHECK_AND_USE(60) CHECK_AND_USE(61) CHECK_AND_USE(62) CHECK_AND_USE(63)
    CHECK_AND_USE(64) CHECK_AND_USE(65) CHECK_AND_USE(66) CHECK_AND_USE(67)
    CHECK_AND_USE(68) CHECK_AND_USE(69) CHECK_AND_USE(70) CHECK_AND_USE(71)
    CHECK_AND_USE(72) CHECK_AND_USE(73) CHECK_AND_USE(74) CHECK_AND_USE(75)
    CHECK_AND_USE(76) CHECK_AND_USE(77) CHECK_AND_USE(78) CHECK_AND_USE(79)
    CHECK_AND_USE(80) CHECK_AND_USE(81) CHECK_AND_USE(82) CHECK_AND_USE(83)
    CHECK_AND_USE(84) CHECK_AND_USE(85) CHECK_AND_USE(86) CHECK_AND_USE(87)
    CHECK_AND_USE(88) CHECK_AND_USE(89) CHECK_AND_USE(90) CHECK_AND_USE(91)
    CHECK_AND_USE(92) CHECK_AND_USE(93) CHECK_AND_USE(94) CHECK_AND_USE(95)
    CHECK_AND_USE(96) CHECK_AND_USE(97) CHECK_AND_USE(98) CHECK_AND_USE(99)
}

// --- Pattern 2: Branch fan-out (tests intersect scaling) ---
// 50 independent if-branches merging at one point

#define BRANCH(N) if (getInt()) { s##N = &nodes[N]; }

void stress_fanout() {
    Node nodes[50];
    Node * _Nullable s0 = nullptr, * _Nullable s1 = nullptr;
    Node * _Nullable s2 = nullptr, * _Nullable s3 = nullptr;
    Node * _Nullable s4 = nullptr, * _Nullable s5 = nullptr;
    Node * _Nullable s6 = nullptr, * _Nullable s7 = nullptr;
    Node * _Nullable s8 = nullptr, * _Nullable s9 = nullptr;
    Node * _Nullable s10 = nullptr, * _Nullable s11 = nullptr;
    Node * _Nullable s12 = nullptr, * _Nullable s13 = nullptr;
    Node * _Nullable s14 = nullptr, * _Nullable s15 = nullptr;
    Node * _Nullable s16 = nullptr, * _Nullable s17 = nullptr;
    Node * _Nullable s18 = nullptr, * _Nullable s19 = nullptr;
    Node * _Nullable s20 = nullptr, * _Nullable s21 = nullptr;
    Node * _Nullable s22 = nullptr, * _Nullable s23 = nullptr;
    Node * _Nullable s24 = nullptr, * _Nullable s25 = nullptr;
    Node * _Nullable s26 = nullptr, * _Nullable s27 = nullptr;
    Node * _Nullable s28 = nullptr, * _Nullable s29 = nullptr;
    Node * _Nullable s30 = nullptr, * _Nullable s31 = nullptr;
    Node * _Nullable s32 = nullptr, * _Nullable s33 = nullptr;
    Node * _Nullable s34 = nullptr, * _Nullable s35 = nullptr;
    Node * _Nullable s36 = nullptr, * _Nullable s37 = nullptr;
    Node * _Nullable s38 = nullptr, * _Nullable s39 = nullptr;
    Node * _Nullable s40 = nullptr, * _Nullable s41 = nullptr;
    Node * _Nullable s42 = nullptr, * _Nullable s43 = nullptr;
    Node * _Nullable s44 = nullptr, * _Nullable s45 = nullptr;
    Node * _Nullable s46 = nullptr, * _Nullable s47 = nullptr;
    Node * _Nullable s48 = nullptr, * _Nullable s49 = nullptr;

    BRANCH(0)  BRANCH(1)  BRANCH(2)  BRANCH(3)  BRANCH(4)
    BRANCH(5)  BRANCH(6)  BRANCH(7)  BRANCH(8)  BRANCH(9)
    BRANCH(10) BRANCH(11) BRANCH(12) BRANCH(13) BRANCH(14)
    BRANCH(15) BRANCH(16) BRANCH(17) BRANCH(18) BRANCH(19)
    BRANCH(20) BRANCH(21) BRANCH(22) BRANCH(23) BRANCH(24)
    BRANCH(25) BRANCH(26) BRANCH(27) BRANCH(28) BRANCH(29)
    BRANCH(30) BRANCH(31) BRANCH(32) BRANCH(33) BRANCH(34)
    BRANCH(35) BRANCH(36) BRANCH(37) BRANCH(38) BRANCH(39)
    BRANCH(40) BRANCH(41) BRANCH(42) BRANCH(43) BRANCH(44)
    BRANCH(45) BRANCH(46) BRANCH(47) BRANCH(48) BRANCH(49)
}

// --- Pattern 3: Many small functions (realistic workload) ---
// 100 functions with typical null-check-and-use patterns

#define SMALL_FN(N) \
    void small_fn_##N(Node * _Nullable p) { \
        if (!p) return; \
        p->value = N; \
        if (p->next) p->next->value = N + 1; \
    }

SMALL_FN(0)  SMALL_FN(1)  SMALL_FN(2)  SMALL_FN(3)  SMALL_FN(4)
SMALL_FN(5)  SMALL_FN(6)  SMALL_FN(7)  SMALL_FN(8)  SMALL_FN(9)
SMALL_FN(10) SMALL_FN(11) SMALL_FN(12) SMALL_FN(13) SMALL_FN(14)
SMALL_FN(15) SMALL_FN(16) SMALL_FN(17) SMALL_FN(18) SMALL_FN(19)
SMALL_FN(20) SMALL_FN(21) SMALL_FN(22) SMALL_FN(23) SMALL_FN(24)
SMALL_FN(25) SMALL_FN(26) SMALL_FN(27) SMALL_FN(28) SMALL_FN(29)
SMALL_FN(30) SMALL_FN(31) SMALL_FN(32) SMALL_FN(33) SMALL_FN(34)
SMALL_FN(35) SMALL_FN(36) SMALL_FN(37) SMALL_FN(38) SMALL_FN(39)
SMALL_FN(40) SMALL_FN(41) SMALL_FN(42) SMALL_FN(43) SMALL_FN(44)
SMALL_FN(45) SMALL_FN(46) SMALL_FN(47) SMALL_FN(48) SMALL_FN(49)
SMALL_FN(50) SMALL_FN(51) SMALL_FN(52) SMALL_FN(53) SMALL_FN(54)
SMALL_FN(55) SMALL_FN(56) SMALL_FN(57) SMALL_FN(58) SMALL_FN(59)
SMALL_FN(60) SMALL_FN(61) SMALL_FN(62) SMALL_FN(63) SMALL_FN(64)
SMALL_FN(65) SMALL_FN(66) SMALL_FN(67) SMALL_FN(68) SMALL_FN(69)
SMALL_FN(70) SMALL_FN(71) SMALL_FN(72) SMALL_FN(73) SMALL_FN(74)
SMALL_FN(75) SMALL_FN(76) SMALL_FN(77) SMALL_FN(78) SMALL_FN(79)
SMALL_FN(80) SMALL_FN(81) SMALL_FN(82) SMALL_FN(83) SMALL_FN(84)
SMALL_FN(85) SMALL_FN(86) SMALL_FN(87) SMALL_FN(88) SMALL_FN(89)
SMALL_FN(90) SMALL_FN(91) SMALL_FN(92) SMALL_FN(93) SMALL_FN(94)
SMALL_FN(95) SMALL_FN(96) SMALL_FN(97) SMALL_FN(98) SMALL_FN(99)

// --- Pattern 4: Deep nesting (tests edge state tracking) ---

void stress_deep_nesting(
    Node * _Nullable p0,  Node * _Nullable p1,  Node * _Nullable p2,
    Node * _Nullable p3,  Node * _Nullable p4,  Node * _Nullable p5,
    Node * _Nullable p6,  Node * _Nullable p7,  Node * _Nullable p8,
    Node * _Nullable p9,  Node * _Nullable p10, Node * _Nullable p11,
    Node * _Nullable p12, Node * _Nullable p13, Node * _Nullable p14) {
    if (p0) {
     if (p1) {
      if (p2) {
       if (p3) {
        if (p4) {
         if (p5) {
          if (p6) {
           if (p7) {
            if (p8) {
             if (p9) {
              if (p10) {
               if (p11) {
                if (p12) {
                 if (p13) {
                  if (p14) {
                    p0->value = p1->value + p2->value + p3->value;
                    p4->value = p5->value + p6->value + p7->value;
                    p8->value = p9->value + p10->value + p11->value;
                    p12->value = p13->value + p14->value;
                  }
                 }
                }
               }
              }
             }
            }
           }
          }
         }
        }
       }
      }
     }
    }
}

// --- Pattern 5: Linked list traversal with operations ---

void stress_linked_list() {
    Node * _Nullable head = getNode();
    int sum = 0;
    for (Node * _Nullable p = head; p; p = p->next) {
        sum += p->value;
        if (p->left) {
            sum += p->left->value;
            if (p->left->right) {
                sum += p->left->right->value;
            }
        }
        if (p->right) {
            sum += p->right->value;
        }
    }
    (void)sum;
}

#pragma clang assume_nonnull end
