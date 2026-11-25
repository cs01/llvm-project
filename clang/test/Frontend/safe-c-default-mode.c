// The driver's default: ownership annotations are not required. What needs
// none is still an error.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: not %clang -fsafe-c -mllvm -safe-c-strict-ownership=true -fsyntax-only %s 2>&1 | FileCheck %s
// CHECK: holds a pointer nothing tracks
#include <stdlib.h>
#include <string.h>
struct node { struct node *_Nullable next; const char *name; int v; };
static int counter;
static const char *names[] = {"a", "b"};
struct node *_Nullable push(struct node *_Nullable head, int v) {
  struct node *_Nullable n = malloc(sizeof *n);
  if (!n) return head;
  n->next = head;
  n->name = names[v & 1];
  n->v = v + counter++;
  return n;
}
int local_use_after_free(void) {
  int *_Nullable p = malloc(sizeof *p); // expected-error {{does not live long enough}}
  if (!p) return 0;
  free(p);   // expected-note {{freed here}}
  return *p; // expected-note {{later used here}}
}
void forged(struct node *__bidi_indexable n, long *__bidi_indexable bytes) {
  memcpy(n, bytes, sizeof *n); // expected-error {{can forge a pointer}}
}
int bounds(int *__counted_by(n) p, int n) { // expected-note 0+ {{}}
  int a[4] = {0}; // expected-note 0+ {{}}
  return bounds(a, 5) + p[0]; // expected-error {{always fails}}
}
