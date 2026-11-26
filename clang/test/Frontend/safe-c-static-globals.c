// A global whose pointers are all '_Static' (or '_Unchecked', or functions)
// frees nothing when it changes, so it may be passed to a function that
// changes it: a ring buffer in a global, over a global array.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
struct ring { unsigned char *_Static _Nullable __counted_by_or_null(size) buf; size_t size; void *_Unchecked _Nullable arg; void (*_Nullable fn)(int); };
static unsigned char memory[64];
static struct ring rx;
static void init(struct ring *r, unsigned char *_Static __counted_by(n) buf, size_t n) { r->buf = buf, r->size = n; }
void good(void) { init(&rx, memory, sizeof memory); }
void dangling(void) {
  unsigned char local[64];
  init(&rx, local, sizeof local); // expected-error {{this parameter is '_Static'}}
}
