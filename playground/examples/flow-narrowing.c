// The compiler follows your null checks through control flow.
// Once a pointer is checked, it's non-null on that path, so real
// code with guards compiles clean and only the gaps get flagged.
//
// Two bugs below. Can you spot them before you hit Compile?

#include <assert.h>
#include <stdio.h>

typedef struct Player {
    int id;
    int score;
    const char* name;
    struct Player* next;
} Player;

Player* _Nullable find_player(int id);
Player* _Nullable find_rival(const Player* p);

int score_gap(int id) {
    Player* p = find_player(id);
    if (!p)
        return 0;

    Player* rival = find_rival(p);
    if (!p)
        return 0;

    return p->score - rival->score;
}

int award_points(int id, int points) {
    Player* p = find_player(id);
    if (!p) {
        fprintf(stderr, "no player %d (%s)\n", id, p->name);
        return -1;
    }
    p->score += points;
    return p->score;
}

// ---- Everything below is safe and compiles without a warning ----

void reset(Player* p) {
    if (!p)
        return;
    p->score = 0;
}

int is_leader(Player* p) {
    return p && p->score > 100;
}

const char* display_name(Player* p) {
    return p ? p->name : "(nobody)";
}

int total_score(Player* head) {
    int total = 0;
    for (Player* it = head; it; it = it->next)
        total += it->score;
    return total;
}

void promote(int id) {
    Player* p = find_player(id);
    assert(p && "promote() called with unknown id");
    p->score *= 2;
}
