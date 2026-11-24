// Tests for safec_std.h. Exits non-zero on the first failure.
#include <stdio.h>

#include "../safec_std.h"

static int failures;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                      \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void test_str(void) {
  str s = str_trim(S("  key = value \n"));
  CHECK(str_eq(s, S("key = value")));

  str rest = S("a,bb,,ccc"), part = S("");
  int n = 0;
  size_t total = 0;
  while (str_split(&rest, ',', &part)) {
    n++;
    total += part.len;
  }
  CHECK(n == 4);
  CHECK(total == 6);

  CHECK(str_eq(str_from("abc"), S("abc")));
  CHECK(!str_eq(S("abc"), S("abd")));
}

static void test_string(void) {
  string out = {0, 0, 0};
  for (int i = 0; i < 100; i++)
    CHECK(string_push(&out, S("xy")));
  CHECK(out.len == 200);
  str v = string_view(&out);
  CHECK(v.len == 200 && v.p && v.p[199] == 'y');
  string_free(&out);
  CHECK(out.len == 0);
}

static void test_vec(void) {
  VEC(int) nums = {0, 0, 0};
  for (int i = 0; i < 1000; i++)
    vec_push(&nums, i);
  long sum = 0;
  vec_each(int x, nums)
    sum += x;
  CHECK(nums.len == 1000);
  CHECK(sum == 999L * 1000 / 2);
  vec_free(&nums);
}

int main(void) {
  test_str();
  test_string();
  test_vec();
  printf(failures ? "%d failed\n" : "ok\n", failures);
  return failures != 0;
}
