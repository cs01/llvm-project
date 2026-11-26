/* Splitting, numbers and growing text, with no strtok, strchr or strcpy. */
#include "../text.h"
/* (exit, not return: a failed check ends the test, with whatever it holds) */
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static long sum(struct str csv) {
  long total = 0;
  for (struct str rest = csv; rest.len; rest = str_after(rest, ',')) {
    str_long n = str_int(str_trim(str_before(rest, ',')));
    if (n.ok)
      total += n.value;
  }
  return total;
}

static int is(str_long r, enum str_number_error e) { return !r.ok && r.err == e; }

int main(void) {
  CHECK(sum(STR("1, 20 ,x,-3,")) == 18);
  CHECK(sum(STR("")) == 0 && sum(STR(",,")) == 0);
  long v = 7;
  str_long a = str_int(STR("9223372036854775807")), b = str_int(STR("-9223372036854775808"));
  CHECK(a.ok && a.value == LONG_MAX && b.ok && b.value == LONG_MIN);
  CHECK(is(str_int(STR("9223372036854775808")), STR_TOO_BIG) && is(str_int(STR("-9223372036854775809")), STR_TOO_BIG));
  CHECK(is(str_int(STR("-")), STR_NOT_A_NUMBER) && is(str_int(STR("")), STR_EMPTY) && is(str_int(STR("1x")), STR_NOT_A_NUMBER));
  CHECK(str_eq(str_after(STR("a=b=c"), '='), STR("b=c")) && str_after(STR("a"), '=').len == 0);
  CHECK(str_eq(str_trim(STR(" \t ")), STR("")) && !str_eq(STR("a"), STR("b")));
  CHECK(str_write(STR(""), "", stdout));
  struct text t = {0};
  CHECK(str_eq(text_str(&t), STR("")));
  for (int i = 0; i < 1000; i++)
    CHECK(text_add(&t, "abc", 3));
  CHECK(t.len == 3000 && str_eq(str_part(text_str(&t), 2997, 99), STR("abc")));
  FILE *_Owned _Nullable f = tmpfile();
  CHECK(f != NULL);
  if (!f)
    exit(1);
  fputs("abcd\n\nxy", f);
  rewind(f);
  int lines = 0;
  for (; text_line(&t, f); lines++)
    v = (long)t.len;
  fclose(f);
  CHECK(lines == 3 && v == 2);
  text_free(&t);
  CHECK(text_str(&t).len == 0);
  struct strings names = {0};
  CHECK(strings_count(&names) == 0 && strings_at(&names, 0).len == 0);
  for (int i = 0; i < 1000; i++)
    CHECK(strings_add(&names, i % 2 ? STR("Ada") : STR("")));
  CHECK(strings_count(&names) == 1000 && str_eq(strings_at(&names, 999), STR("Ada")));
  CHECK(str_eq(strings_at(&names, 998), STR("")) && strings_at(&names, 1000).len == 0);
  strings_free(&names);
  CHECK(strings_count(&names) == 0 && strings_at(&names, 0).len == 0 && strings_add(&names, STR("x")));
  strings_free(&names);
  puts("ok");
  return 0;
}
