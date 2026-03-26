// A real bug pattern: you check one pointer but use another.
// Standard Clang sees nothing wrong. Nullsafe Clang catches it.

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
