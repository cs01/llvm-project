/* Linked into every Juliet program by juliet.ts: rand() returns the numbers in
 * JULIET_RAND (comma separated, repeated), so the harness can choose what a
 * "random" source reads. Without the variable it always returns 1. */
#include <stdlib.h>
int rand(void) {
  static const char *list, *at;
  if (!list) {
    list = getenv("JULIET_RAND");
    if (!list || !*list)
      list = "1";
    at = list;
  }
  char *end;
  long v = strtol(at, &end, 10);
  at = *end == ',' ? end + 1 : list;
  return (int)v;
}
