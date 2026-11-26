#include <stdlib.h>
struct node { int v; struct node *_Owned _Nullable next; };

int main(void) {
  struct node *_Owned _Nullable n = calloc(1, sizeof *n);
  if (!n) return 1;
  free(n);
  return n->v;            // used after free
  // error: 'n' is used after it was freed on line 7
}
