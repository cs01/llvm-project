// Ownership on ClangIR is the driver's default; one switch leaves it out for a file.
// REQUIRES: system-darwin
// RUN: not %clang -fsafe-c -fsyntax-only %s 2>&1 | FileCheck %s
// RUN: %clang -fsafe-c -mllvm -safe-c-ownership=false -fsyntax-only %s
// CHECK: :[[@LINE+7]]:{{[0-9]+}}: error: 'n' is freed after it was stored on line
// CHECK-NOT: error:
#include <stdio.h>
#include <stdlib.h>
struct node { int v; struct node *_Owned _Nullable next; };
struct list { struct node *_Owned _Nullable head; };
static void node_free(struct node *_Owned _Nullable n);
void push(struct list *l, struct node *_Owned n) { node_free(_Take(l->head)); l->head = n; free(n); }
void clear(struct list *l) {
  while (l->head) {
    struct node *_Owned _Nullable n = _Take(l->head);
    if (!n)
      return;
    l->head = _Take(n->next);
    free(n);
  }
}
int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  fprintf(stderr, "%d\n", argc);
  return argc > 1 ? puts(argv[1]) : 0;
}

// Frees a node and the rest of its list.
static void node_free(struct node *_Owned _Nullable n) {
  if (!n)
    return;
  node_free(_Take(n->next));
  free(n);
}
