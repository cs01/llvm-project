// Bug patterns standard Clang misses entirely.
// Nullsafe Clang catches them all flow-sensitively.

typedef struct {
    int x, y;
} Point;

Point* _Nullable find_nearest(int x, int y);
Point* _Nullable find_farthest(int x, int y);

void draw_line(void) {
    Point* start = find_nearest(0, 0);
    Point* end = find_farthest(0, 0);

    if (start) {
        // Checked start, but forgot to check end.
        int dx = end->x - start->x;  // BUG: end might be NULL
        int dy = end->y - start->y;  // BUG: end might be NULL
    }
}

// Fixed version:
void draw_line_safe(void) {
    Point* start = find_nearest(0, 0);
    Point* end = find_farthest(0, 0);

    if (start && end) {
        int dx = end->x - start->x;  // OK — both checked
        int dy = end->y - start->y;  // OK
    }
}

// Assignment to _Nonnull is also checked flow-sensitively
void assign_checked(void) {
    Point* _Nullable p = find_nearest(0, 0);
    Point* _Nonnull safe = p;  // warning: assigning nullable to nonnull

    if (p) {
        Point* _Nonnull safe2 = p;  // OK — p was checked
    }
}
