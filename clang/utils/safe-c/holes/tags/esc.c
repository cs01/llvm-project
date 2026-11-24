#include <stdio.h>
#include <safec.h>
struct t { int k; union _Tagged_by(k) { const char *_Static p _When(1); long l _When(0); }; };
static void show(struct t t) { if (t.k == 1) puts(t.p); }
static void showp(struct t *t) { if (t->k == 1) puts(t->p); }
static struct t mk(void) { struct t t = {.k = 0, .l = 77}; t.k = 1; return t; }
int main(int c, char **v) { struct t t = {.k = 0, .l = 77}; t.k = 1; if (c == 1) show(t); else if (c == 2) showp(&t); else show(mk()); return 0; }
