/* sort [-nru] [-t c] [-k column] [file ...]: ported from suckless sbase (MIT), with fewer options.
   All lines are in one text; what is sorted is where each one is. */
#include "../args.h"

struct span { size_t at, len; };
#define VEC_NAME spans
#define VEC_TYPE struct span
#include "../vec.h"
struct options { int number, reverse, unique, separator; long column; };

static struct str skip(struct str s, int separator) {
  if (separator)
    return str_after(s, (char)separator);
  size_t i = 0;
  while (i < s.len && (s.p[i] == ' ' || s.p[i] == '\t'))
    i++;
  while (i < s.len && s.p[i] != ' ' && s.p[i] != '\t')
    i++;
  return str_part(s, i, s.len);
}

static struct str key(struct str s, struct options o) {
  for (long i = 1; i < o.column; i++)
    s = skip(s, o.separator);
  return s;
}

static double number(struct str s) {
  struct text t = {0};
  double d = 0;
  if (text_add(&t, s.p, s.len) && text_add(&t, "\0", 1) && t.p)
    d = strtod(t.p, NULL);
  text_free(&t);
  return d;
}

static int compare(struct str a, struct str b, struct options o) {
  int r = 0;
  if (o.number) {
    double x = number(key(a, o)), y = number(key(b, o));
    r = (x > y) - (x < y);
  } else {
    r = str_cmp(key(a, o), key(b, o));
  }
  if (r == 0 && !o.unique)
    r = str_cmp(a, b);
  return o.reverse ? -r : r;
}

static struct str line(const struct text *all, struct span s) {
  return str_part(text_str(all), s.at, s.len);
}

/* Merge sort: from[lo, hi) into to, both in order afterwards. */
static void sort(struct span *__counted_by(n) from, struct span *__counted_by(n) to, size_t n,
                 size_t lo, size_t hi, const struct text *all, struct options o) {
  if (hi - lo < 2)
    return;
  size_t mid = lo + (hi - lo) / 2, i = lo, j = mid;
  sort(to, from, n, lo, mid, all, o);
  sort(to, from, n, mid, hi, all, o);
  for (size_t k = lo; k < hi; k++)
    if (j == hi || (i < mid && compare(line(all, from[i]), line(all, from[j]), o) <= 0))
      to[k] = from[i++];
    else
      to[k] = from[j++];
}

static int read_lines(FILE *f, struct text *all, struct spans *lines) {
  struct text one = {0};
  int ok = 1;
  while (ok && text_line(&one, f)) {
    struct span s = {all->len, one.len};
    struct str t = text_str(&one);
    ok = text_add(all, t.p, t.len) && spans_add(lines, s);
  }
  text_free(&one);
  return ok;
}

static int output(struct spans *lines, const struct text *all, struct options o) {
  size_t n = lines->len;
  if (n == 0)
    return 1;
  struct span *_Owned _Nullable a = calloc(n, sizeof(struct span));
  struct span *_Owned _Nullable b = calloc(n, sizeof(struct span));
  if (a && b) {
    for (size_t i = 0; i < n; i++)
      spans_get(lines, i, &a[i]), b[i] = a[i];
    sort(a, b, n, 0, n, all, o);
    for (size_t i = 0; i < n; i++)
      if (!o.unique || i == 0 || compare(line(all, b[i]), line(all, b[i - 1]), o) != 0) {
        struct str s = line(all, b[i]);
        fwrite(s.p, 1, s.len, stdout);
        putchar('\n');
      }
  }
  int ok = a && b;
  free(a);
  free(b);
  return ok;
}

static int column(int argc, char *__null_terminated _Static const *__counted_by(argc) argv, long *out) {
  str_long n = args_int(argc, argv, "-k");
  if (!n.ok)
    return 0;
  *out = n.value;
  return 1;
}

int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  struct options o = {0, 0, 0, 0, 1};
  int i = 1;
  for (; i < argc && args_flags(argv[i]); i++) {
    if (strcmp(argv[i], "-k") == 0 && column(argc, argv, &o.column))
      i++;
    else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
      o.separator = *argv[++i];
    else
      for (const char *__null_terminated p = argv[i] + 1; *p; p++) {
        if (*p == 'n')
          o.number = 1;
        else if (*p == 'r')
          o.reverse = 1;
        else if (*p == 'u')
          o.unique = 1;
        else
          return fputs("usage: sort [-nru] [-t c] [-k column] [file ...]\n", stderr), 2;
      }
  }
  struct text all = {0};
  struct spans lines = {0};
  int ok = 1;
  if (i == argc)
    ok = read_lines(stdin, &all, &lines);
  for (; ok && i < argc; i++) {
    FILE *_Owned _Nullable f = fopen(argv[i], "r");
    if (!f)
      return perror(argv[i]), 2;
    ok = read_lines(f, &all, &lines);
    fclose(f);
  }
  ok = ok && output(&lines, &all, o);
  text_free(&all);
  spans_free(&lines);
  return ok ? 0 : 2;
}
