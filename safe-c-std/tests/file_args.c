/* Files and options. */
#include "../args.h"
#include "../file.h"
#include <errno.h>
/* (exit, not return: a failed check ends the test, with whatever it holds) */
#define CHECK(x) do { if (!(x)) { printf("line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  CHECK(args_find(argc, argv, "--no") == 0 && args_value(argc, argv, "--no") == 0);
  struct text t = {0};
  file_result no = file_read("/nonexistent/x", &t), yes = file_read("/etc/hosts", &t);
  CHECK(!no.ok && no.err == ENOENT && yes.ok && yes.value == t.len && t.len > 0);
  text_free(&t);
  CHECK(file_write("/tmp/safec_file_test", STR("hello")).ok && file_read("/tmp/safec_file_test", &t).ok);
  CHECK(str_eq(text_str(&t), STR("hello")));
  text_free(&t);
  CHECK(!file_write("/nonexistent/x", STR("a")).ok);
  remove("/tmp/safec_file_test");
  if (argc == 5) {
    str_long n = args_int(argc, argv, "--n");
    CHECK(args_find(argc, argv, "-v") == 1 && n.ok && n.value == 42);
    CHECK(args_value(argc, argv, "last") == 0 && !args_int(argc, argv, "-v").ok && !args_int(argc, argv, "--none").ok);
  }
  puts("ok");
  return 0;
}
