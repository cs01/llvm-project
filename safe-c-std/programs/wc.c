/* wc [-clw] [file ...]: ported from suckless sbase (MIT). Bytes, not UTF-8. */
#include "../args.h"
#include <ctype.h>

struct counts { size_t bytes, lines, words; };
struct shown { int bytes, lines, words; };

static void put(size_t n, int first) {
  if (!first)
    putchar(' ');
  printf("%zu", n);
}

static void output(const char *__null_terminated _Nullable name, struct counts n, struct shown s) {
  int first = 1;
  if (s.lines)
    printf("%zu", n.lines), first = 0;
  if (s.words)
    put(n.words, first), first = 0;
  if (s.bytes)
    put(n.bytes, first);
  if (name)
    printf(" %s", name);
  putchar('\n');
}

static struct counts count(FILE *f) {
  struct counts n = {0, 0, 0};
  int word = 0;
  for (int c = getc(f); c != EOF; c = getc(f)) {
    n.bytes++;
    n.lines += c == '\n';
    if (!isspace(c))
      word = 1;
    else if (word)
      word = 0, n.words++;
  }
  n.words += word != 0;
  return n;
}

int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  struct shown s = {0, 0, 0};
  int i = 1;
  for (; i < argc && args_flags(argv[i]); i++)
    for (const char *__null_terminated p = argv[i] + 1; *p; p++) {
      if (*p == 'c')
        s.bytes = 1;
      else if (*p == 'l')
        s.lines = 1;
      else if (*p == 'w')
        s.words = 1;
      else
        return fputs("usage: wc [-c] [-lw] [file ...]\n", stderr), 1;
    }
  if (!s.bytes && !s.lines && !s.words)
    s.bytes = s.lines = s.words = 1;
  if (i == argc)
    output(NULL, count(stdin), s);
  struct counts total = {0, 0, 0};
  int many = argc - i > 1, failed = 0;
  for (; i < argc; i++) {
    struct counts n;
    if (strcmp(argv[i], "-") == 0) {
      n = count(stdin);
    } else {
      FILE *_Owned _Nullable f = fopen(argv[i], "r");
      if (!f) {
        perror(argv[i]);
        failed = 1;
        continue;
      }
      n = count(f);
      fclose(f);
    }
    total.bytes += n.bytes, total.lines += n.lines, total.words += n.words;
    output(strcmp(argv[i], "-") == 0 ? "<stdin>" : argv[i], n, s);
  }
  if (many)
    output("total", total, s);
  return failed;
}
