/* comm [-123] file1 file2: ported from suckless sbase (MIT). */
#include "../args.h"

static void print(int column, struct str s, const int *__counted_by(3) show) {
  if (!show[column])
    return;
  for (int i = 0; i < column; i++)
    if (show[i])
      putchar('\t');
  fwrite(s.p, 1, s.len, stdout);
  putchar('\n');
}

static void comm(FILE *a, FILE *b, const int *__counted_by(3) show) {
  struct text x = {0}, y = {0};
  int more_x = text_line(&x, a), more_y = text_line(&y, b);
  while (more_x || more_y) {
    int d = !more_x ? 1 : !more_y ? -1 : str_cmp(text_str(&x), text_str(&y));
    if (d < 0)
      print(0, text_str(&x), show);
    else if (d > 0)
      print(1, text_str(&y), show);
    else
      print(2, text_str(&x), show);
    if (d <= 0)
      more_x = text_line(&x, a);
    if (d >= 0)
      more_y = text_line(&y, b);
  }
  text_free(&x);
  text_free(&y);
}

int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  int show[3] = {1, 1, 1};
  int i = 1;
  for (; i < argc && args_flags(argv[i]); i++)
    for (const char *__null_terminated p = argv[i] + 1; *p; p++) {
      if (*p < '1' || *p > '3')
        return fputs("usage: comm [-123] file1 file2\n", stderr), 1;
      show[*p - '1'] = 0;
    }
  if (argc - i != 2)
    return fputs("usage: comm [-123] file1 file2\n", stderr), 1;
  FILE *_Owned _Nullable a = fopen(argv[i], "r");
  if (!a)
    return perror(argv[i]), 1;
  FILE *_Owned _Nullable b = fopen(argv[i + 1], "r");
  if (!b) {
    perror(argv[i + 1]);
    fclose(a);
    return 1;
  }
  comm(a, b, show);
  fclose(a);
  fclose(b);
  return 0;
}
