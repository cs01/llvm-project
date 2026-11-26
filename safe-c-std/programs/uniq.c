/* uniq [-c] [-d | -u] [-f n] [-s n] [file]: ported from suckless sbase (MIT). */
#include "../args.h"

struct options { int count, repeated, single; long fields, chars; };

static int blank(char c) { return c == ' ' || c == '\t'; }

static struct str key(struct str s, struct options o) {
  size_t i = 0;
  for (long f = o.fields; f > 0 && i < s.len; f--) {
    while (i < s.len && blank(s.p[i]))
      i++;
    while (i < s.len && !blank(s.p[i]))
      i++;
  }
  for (long n = o.chars; n > 0 && i < s.len; n--)
    i++;
  return str_part(s, i, s.len);
}

static void print(const struct text *line, long times, struct options o) {
  if (times == 0 || (times == 1 && o.repeated) || (times > 1 && o.single))
    return;
  if (o.count)
    printf("%7ld ", times);
  struct str s = text_str(line);
  fwrite(s.p, 1, s.len, stdout);
  putchar('\n');
}

static int uniq(FILE *f, struct options o) {
  struct text line = {0}, last = {0};
  long times = 0;
  while (text_line(&line, f)) {
    if (times && str_eq(key(text_str(&line), o), key(text_str(&last), o))) {
      times++;
      continue;
    }
    print(&last, times, o);
    if (!text_set(&last, text_str(&line))) {
      text_free(&line);
      text_free(&last);
      return 1;
    }
    times = 1;
  }
  print(&last, times, o);
  text_free(&line);
  text_free(&last);
  return 0;
}

/* The number after argv[i]. */
static int number(int argc, char *__null_terminated _Static const *__counted_by(argc) argv, int i, long *out) {
  str_long n = args_int(argc, argv, argv[i]);
  if (!n.ok)
    return 0;
  *out = n.value;
  return 1;
}

int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  struct options o = {0, 0, 0, 0, 0};
  int i = 1;
  for (; i < argc && args_flags(argv[i]); i++) {
    if (strcmp(argv[i], "-c") == 0)
      o.count = 1;
    else if (strcmp(argv[i], "-d") == 0)
      o.repeated = 1;
    else if (strcmp(argv[i], "-u") == 0)
      o.single = 1;
    else if (strcmp(argv[i], "-f") == 0 && number(argc, argv, i, &o.fields))
      i++;
    else if (strcmp(argv[i], "-s") == 0 && number(argc, argv, i, &o.chars))
      i++;
    else
      return fputs("usage: uniq [-c] [-d | -u] [-f n] [-s n] [file]\n", stderr), 1;
  }
  if (i == argc || strcmp(argv[i], "-") == 0)
    return uniq(stdin, o);
  FILE *_Owned _Nullable f = fopen(argv[i], "r");
  if (!f)
    return perror(argv[i]), 1;
  int r = uniq(f, o);
  fclose(f);
  return r;
}
