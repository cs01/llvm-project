int from_pch(int *p, int n) pre (p != 0) pre (n > 0);
int *post_from_pch(int n) pre (n > 0) post (r: r != 0);
unsigned long old_from_pch(unsigned long cap) post (r: r <= old(cap));
