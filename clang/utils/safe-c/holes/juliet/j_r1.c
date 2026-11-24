#include <stdio.h>
#include <stdlib.h>
typedef struct { const char *_Static s; } P;
int main(void) {
  for (int k = 0; k < 3; k++) { unsigned char *p = (unsigned char *)malloc(4000); if (p == NULL) { exit(1); } for (int i = 0; i < 4000; i++) { p[i] = 0x41; } free(p); }
  P *p = (P *)malloc(sizeof(P)); if (p == NULL) { exit(1); }
  p[0].s = "a";
  P *q = (P *)realloc(p, 500 * sizeof(P)); if (q == NULL) { exit(1); }
  fprintf(stderr, "%d\n", q[300].s == NULL);
  char c = *q[300].s;
  printf("%c\n", c);
  free(q);
}
