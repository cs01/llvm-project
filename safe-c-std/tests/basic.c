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

static void test_drop(void) {
  for (int i = 0; i < 100; i++) {
    string s _Drop(string_free) = {0, 0, 0}; /* freed at the end of each pass */
    CHECK(string_push(&s, S("hello")));
    if (i % 2)
      continue; /* early exits free it too */
    CHECK(s.len == 5);
  }
}

static void test_io(void) {
  const char *_Nullable __null_terminated tmp = getenv("TMPDIR");
  str dir = tmp ? str_from(tmp) : S("/tmp/");
  string path _Drop(string_free) = {0, 0, 0};
  CHECK(string_push(&path, dir) && string_push(&path, S("/safec_std_io.txt")));
  string name _Drop(string_free) = {0, 0, 0};
  CHECK(string_push(&name, string_view(&path)) && string_push(&name, S("\0")));
  FILE *_Nullable f = name.p ? fopen(name.p, "w") : NULL;
  CHECK(f != NULL);
  if (!f)
    return;
  fputs("alpha\n  beta  \ngamma", f);
  fclose(f);

  string all _Drop(string_free) = {0, 0, 0};
  CHECK(read_file(string_view(&path), &all));
  CHECK(all.len == 20); /* "alpha\n" + "  beta  \n" + "gamma" */

  lines it _Drop(lines_close) = {0, {0, 0, 0}};
  CHECK(lines_open(string_view(&path), &it));
  int n = 0;
  bool second_ok = false;
  while (lines_next(&it)) {
    n++;
    if (n == 2)
      second_ok = str_eq(str_trim(lines_line(&it)), S("beta"));
  }
  CHECK(n == 3 && second_ok);
  CHECK(write_all(1, S("io ")));
}

typedef SLICE(int) int_slice;
static long sum(int_slice s) {
  long t = 0;
  for (size_t i = 0; i < s.len; i++)
    t += s.p[i];
  return t;
}

static long_result add_two(str a, str b) {
  long x = TRY(str_to_long(a), long_result);
  long y = TRY(str_to_long(b), long_result);
  return OK(long_result, x + y);
}

static void test_result(void) {
  int arr[4] = {1, 2, 3, 4};
  CHECK(sum((int_slice){arr, 4}) == 10);
  long_result ok2 = add_two(S("40"), S("2"));
  CHECK(ok2.ok && ok2.value == 42);
  long_result bad2 = add_two(S("40"), S("x"));
  CHECK(!bad2.ok && bad2.err == PARSE_NOT_A_NUMBER);
  long_result r = str_to_long(S("-1234"));
  CHECK(r.ok && r.value == -1234);
  long_result bad = str_to_long(S("12x"));
  CHECK(!bad.ok);
  if (!bad.ok)
    CHECK(bad.err == PARSE_NOT_A_NUMBER);
  long_result big = str_to_long(S("99999999999999999999"));
  CHECK(!big.ok);
}

int main(void) {
  test_str();
  test_string();
  test_vec();
  test_drop();
  test_io();
  test_result();
  printf(failures ? "%d failed\n" : "ok\n", failures);
  return failures != 0;
}
