/* Prints the JSON on stdin with no spaces; 1 if it is not JSON. */
#include "../json.h"

static void quote(struct str s) {
  putchar('"');
  for (size_t i = 0; i < s.len; i++) {
    unsigned char c = (unsigned char)s.p[i];
    if (c == '"' || c == '\\')
      putchar('\\'), putchar(c);
    else if (c < 0x20)
      printf("\\u%04x", c);
    else
      putchar(c);
  }
  putchar('"');
}

static void print(const struct json *j, size_t i) {
  struct json_value v = json_at(j, i);
  switch (v.kind) {
  case JSON_NULL: fputs("null", stdout); break;
  case JSON_BOOL: fputs(v.truth ? "true" : "false", stdout); break;
  case JSON_NUMBER: printf("%.17g", v.number); break;
  case JSON_TEXT: quote(json_text(j, i)); break;
  case JSON_ARRAY:
  case JSON_OBJECT: {
    int object = v.kind == JSON_OBJECT;
    putchar(object ? '{' : '[');
    for (size_t k = json_first(j, i); k; k = json_at(j, k).next) {
      if (k != json_first(j, i))
        putchar(',');
      if (object)
        quote(json_key(j, k)), putchar(':');
      print(j, k);
    }
    putchar(object ? '}' : ']');
  }
  }
}

int main(void) {
  struct text in = {0};
  struct json j = {0};
  char part[4096];
  size_t n = 0;
  while ((n = fread(part, 1, sizeof part, stdin)) > 0)
    if (!text_add(&in, part, n))
      return 1;
  json_result r = json_parse(&j, text_str(&in));
  int ok = r.ok;
  if (r.ok)
    print(&j, 0), putchar('\n');
  else
    fprintf(stderr, "not JSON, at byte %zu\n", r.err.at);
  json_free(&j);
  text_free(&in);
  return !ok;
}
