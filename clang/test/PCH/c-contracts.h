int from_pch(int *p, int n) requires (p != 0) requires (n > 0);
int *post_from_pch(int n) requires (n > 0) ensures (r: r != 0);
unsigned long old_from_pch(unsigned long cap) ensures (r: r <= old(cap));
