// Owners inside structs: a struct is freed after what its fields own, an
// owning field is given a value only when it holds nothing, a struct on the
// stack ends with its owning fields empty, and an '_Empties' parameter is left
// that way.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
#include <string.h>

struct node {
  int value;
  char *_Owned _Nullable __null_terminated name;  /* owns its name */
  struct node *_Owned _Nullable next;             /* owns the rest of the list */
};

/* LEAK 1: frees the node, but its name and the rest of the list are lost. */
void node_free_wrong(struct node *_Owned n) {
  free(n); // expected-error {{'n' is freed while 'n->name' may still own memory}}
}

/* OK: free what the node owns first. _Take moves each owner out and leaves null. */
void node_free(struct node *_Owned _Nullable n) {
  if (!n)
    return;
  node_free(_Take(n->next));
  free(_Take(n->name));
  free(n);
}

/* LEAK 2: setting an owning field to NULL loses whatever it held. */
void cut_wrong(struct node *n) {
  n->next = NULL; // expected-error {{'n->next' may still own memory when it is given another value}}
}

/* OK: free it on the way out. */
void cut(struct node *n) {
  node_free(_Take(n->next));
}

/* LEAK 3: the name is set, then the node is freed without it. */
void make_and_drop_wrong(void) {
  struct node *_Owned _Nullable n = calloc(1, sizeof *n);
  if (!n)
    return;
  n->name = strdup("Ada");
  free(n); // expected-error {{'n' is freed while 'n->name' may still own memory}}
}

/* OK */
void make_and_drop(void) {
  struct node *_Owned _Nullable n = calloc(1, sizeof *n);
  if (!n)
    return;
  n->name = strdup("Ada");
  node_free(n);
}

/* A struct kept on the stack that owns memory. */
struct buffer {
  size_t cap;
  char *_Owned _Nullable __counted_by(cap) bytes;
};

static void buffer_free(struct buffer *_Empties b) {
  free(_Take(b->bytes)); /* (_Take also sets cap to 0) */
}

/* LEAK 4: the buffer's bytes are never freed when the function returns. */
int use_wrong(void) {
  struct buffer b = {0, NULL};
  char *_Owned _Nullable m = calloc(64, 1);
  if (!m)
    return 1;
  b.bytes = m, b.cap = 64;
  return b.bytes ? b.bytes[0] : 0; // expected-error {{'b.bytes' may still own memory when the function returns}}
}

/* OK */
int use(void) {
  struct buffer b = {0, NULL};
  char *_Owned _Nullable m = calloc(64, 1);
  if (!m)
    return 1;
  b.bytes = m, b.cap = 64;
  int r = b.bytes ? b.bytes[0] : 0;
  buffer_free(&b);
  return r;
}

/* A promise to empty is checked. */
static void buffer_forget(struct buffer *_Empties b) {
  (void)b;
} // expected-error {{'b->bytes' may still own memory when the function returns; an '_Empties' parameter is left empty}}

/* A list from calloc starts empty; a field tested null holds nothing. */
void push(struct node *p, struct node *_Owned n) {
  if (p->next) {
    node_free(n);
    return;
  }
  p->next = n;
}
struct node *_Owned _Nullable fresh(void) {
  struct node *_Owned _Nullable n = calloc(1, sizeof *n);
  if (!n)
    return NULL;
  n->name = strdup("x");
  return n;
}
/* Freeing a struct from a field frees only it, not what it owns. */
void cut_one(struct node *n) {
  free(_Take(n->next)); // expected-error {{this frees a struct whose fields may still own memory}}
}
