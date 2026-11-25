// libcaca. The field that pointed at a literal goes away: the name follows from the enum.
enum { MONO, GRAY, C8, C16, FULLGRAY, FULL8, FULL16 };
typedef struct caca_dither { int color; } caca_dither_t;
_Bool same(const char *a, const char *b) { for (; *a && *a == *b; a++) b++; return *a == *b; }
const char *color_name(const caca_dither_t *d) {
  switch (d->color) { case MONO: return "mono"; case GRAY: return "gray"; case C8: return "8"; case C16: return "16"; case FULLGRAY: return "fullgray"; case FULL8: return "full8"; default: return "full16"; }
}
int caca_set_dither_color(caca_dither_t *d, const char *str) {
  if (same(str, "mono")) d->color = MONO;
  else if (same(str, "gray")) d->color = GRAY;
  else if (same(str, "8")) d->color = C8;
  else if (same(str, "16")) d->color = C16;
  else if (same(str, "fullgray")) d->color = FULLGRAY;
  else if (same(str, "full8")) d->color = FULL8;
  else if (same(str, "full16") || same(str, "default")) d->color = FULL16;
  else return -1;
  return 0;
}
