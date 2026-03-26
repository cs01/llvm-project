// The compiler tracks null checks through control flow.
// It catches bugs when you forget a check, AND knows
// when a check makes further warnings unnecessary.

// Can you spot the bug?
typedef struct { int id; int score; } Player;
Player* _Nullable lookup_player(int id);

int get_score(int id) {
    Player* p = lookup_player(id);
    if (!p)
        return -1;

    Player* opponent = lookup_player(p->id + 1);
    if (!p)          // BUG: copy-paste — checks p again instead of opponent
        return -1;

    return p->score - opponent->score;  // opponent might be NULL!
}

// --- Safe patterns the compiler recognizes ---

void guard_clause(int* data) {
    if (!data) return;

    // data is proven non-null from here on
    *data = 42;  // OK — no warning
}

void if_else(int* p) {
    if (p) {
        *p = 1;  // OK — p checked
    } else {
        // p is known NULL here — any use would warn
    }
}

void and_pattern(int* p, int* q) {
    if (p && q) {
        *p = *q;  // OK — both checked
    }
}

void ternary(int* p) {
    int val = p ? *p : 0;  // OK — guarded by ternary
}

// Assertion macros work too — any [[noreturn]] function
extern _Noreturn void abort(void);
#define ASSERT(x) do { if (!(x)) abort(); } while(0)

void with_assert(int* p) {
    ASSERT(p);
    *p = 42;  // OK — ASSERT proved p is non-null
}
