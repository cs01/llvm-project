/* Splitting, numbers and growing text, with no strtok, strchr or strcpy. */
#include "../text.h"
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static long sum(struct str csv) {
  long total = 0, n;
  for (struct str rest = csv; rest.len; rest = str_after(rest, ','))
    if (str_int(str_trim(str_before(rest, ',')), &n))
      total += n;
  return total;
}

int main(void) {
  CHECK(sum(STR("1, 20 ,x,-3,")) == 18);
  CHECK(sum(STR("")) == 0 && sum(STR(",,")) == 0);
  long v = 7;
  CHECK(str_int(STR("9223372036854775807"), &v) && v == LONG_MAX);
  CHECK(str_int(STR("-9223372036854775808"), &v) && v == LONG_MIN);
  CHECK(!str_int(STR("9223372036854775808"), &v) && !str_int(STR("-9223372036854775809"), &v));
  CHECK(!str_int(STR("-"), &v) && !str_int(STR(""), &v) && !str_int(STR("1x"), &v));
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
    return 1;
  fputs("abcd\n\nxy", f);
  rewind(f);
  int lines = 0;
  for (; text_line(&t, f); lines++)
    v = (long)t.len;
  fclose(f);
  CHECK(lines == 3 && v == 2);
  text_free(&t);
  CHECK(text_str(&t).len == 0);
  puts("ok");
  return 0;
}
