int from_pch(int *p, int n) pre (p != 0) pre (n > 0);
int *post_from_pch(int n) pre (n > 0) post (r: r != 0);
unsigned long old_from_pch(unsigned long cap) post (r: r <= old(cap));

// An 'assigns' carries targets rather than a predicate, and they were not
// serialized: the clause came back invalid, so a header's frame condition
// silently contributed nothing.
void assigns_from_pch(int *buf, unsigned len)
  pre     (len > 0)
  assigns (buf[0 : len]);
