/* Files and options. */
#include "../args.h"
#include "../file.h"
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  CHECK(args_find(argc, argv, "--no") == 0 && args_value(argc, argv, "--no") == 0);
  struct text t = {0};
  CHECK(!file_read("/nonexistent/x", &t) && t.len == 0);
  CHECK(file_read("/etc/hosts", &t) && t.len > 0);
  text_free(&t);
  CHECK(file_write("/tmp/safec_file_test", STR("hello")) && file_read("/tmp/safec_file_test", &t));
  CHECK(str_eq(text_str(&t), STR("hello")));
  text_free(&t);
  CHECK(!file_write("/nonexistent/x", STR("a")));
  remove("/tmp/safec_file_test");
  if (argc == 5) {
    long n = 0;
    CHECK(args_find(argc, argv, "-v") == 1 && args_int(argc, argv, "--n", &n) && n == 42);
    CHECK(args_value(argc, argv, "last") == 0 && !args_int(argc, argv, "-v", &n));
  }
  puts("ok");
  return 0;
}
