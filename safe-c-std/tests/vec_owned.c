/* A vector of owners: strings from strdup, and structs that own a string. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* (exit, not return: a failed check ends the test, with whatever it holds) */
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

#define VEC_NAME names
#define VEC_TYPE char *_Owned _Nullable __null_terminated
#define VEC_BORROW char *_Nullable __null_terminated
#define VEC_OWNED 1
#include "../vec.h"

struct person { int age; char *_Owned _Nullable __null_terminated name; };
static void person_free(struct person *_Owned _Nullable p) {
  if (!p) return;
  free(_Take(p->name));
  free(p);
}
#define VEC_NAME people
#define VEC_TYPE struct person *_Owned _Nullable
#define VEC_BORROW struct person *_Nullable
#define VEC_OWNED 1
#define VEC_FREE(x) person_free(x)
#include "../vec.h"

int main(void) {
  struct names n = {0};
  for (int i = 0; i < 100; i++)
    CHECK(names_add(&n, strdup(i % 2 ? "Ada" : "Grace")));
  CHECK(n.len == 100);
  char *_Nullable __null_terminated first = names_at(&n, 0);
  CHECK(first && strcmp(first, "Grace") == 0);
  CHECK(names_at(&n, 100) == NULL);
  char *_Owned _Nullable __null_terminated mine = names_take(&n, 1);
  int took = mine && strcmp(mine, "Ada") == 0 && names_at(&n, 1) == NULL;
  free(mine);
  CHECK(took);
  char *_Owned _Nullable __null_terminated last = names_pop(&n);
  int popped = last && strcmp(last, "Ada") == 0 && n.len == 99;
  free(last);
  CHECK(popped);
  names_free(&n);
  char *_Owned _Nullable __null_terminated none = names_pop(&n);
  int empty = none == NULL;
  free(none);
  CHECK(n.len == 0 && empty);
  names_free(&n); /* (names_pop takes the vector, so it might have refilled it) */

  struct people ps = {0};
  for (int i = 0; i < 20; i++) {
    struct person *_Owned _Nullable p = calloc(1, sizeof *p);
    if (!p) exit(1);
    p->age = i;
    p->name = strdup("x");
    CHECK(people_add(&ps, p));
  }
  struct person *_Nullable p7 = people_at(&ps, 7);
  CHECK(p7 && p7->age == 7 && p7->name && strcmp(p7->name, "x") == 0);
  people_free(&ps);
  puts("ok");
  return 0;
}
