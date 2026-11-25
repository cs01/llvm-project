// Main's arguments can be passed on as 'char *_Static const *'; '= {0}' fills a
// nested struct; a result is kept past a temporary argument.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
#include <string.h>
static int find(int argc, char *__null_terminated _Static const *__counted_by(argc) argv,
                const char *__null_terminated name) {
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], name) == 0)
      return i;
  return 0;
}
static int on(int argc, char *__null_terminated _Static const *__counted_by(argc) argv) {
  return find(argc, argv, "-v");
}
static int swap(int argc, char *__null_terminated _Static const *__counted_by(argc) a,
                char *__null_terminated _Static const *__counted_by(argc) b) {
  a = b; // expected-error {{'a' keeps the array it was given; use an index}}
  argc = argc;
  b = b;
  return argc;
}
int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  return on(argc, argv) + find(argc, argv, "-x");
}

struct in { size_t n; int *_Owned _Nullable __counted_by(n) p; long a[4]; };
struct out { int k; struct in i; };
int zero(void) {
  struct out x = {0};
  size_t n = 1;
  int *_Owned _Nullable m = malloc(n * sizeof(int));
  x.i.p = m, x.i.n = n;
  int *_Nullable q = x.i.p;
  free(_Take(x.i.p));
  return q ? x.k : 0; // expected-error {{'q' is tested after '_Take'}}
}

struct _View v { const char *__counted_by(n) p; size_t n; };
static int *_Nullable get(struct out *o, struct v key) {
  return key.n ? &o->k : NULL;
}
int kept(void) {
  struct out *_Owned _Nullable o = calloc(1, sizeof *o);
  if (!o) return 1;
  int *_Nullable p = get(o, (struct v){"a", 1});
  int r = p ? *p : 0;
  free(_Take(o->i.p));
  free(o);
  return p ? r : 0; // expected-error {{'p' is tested after}}
}
