/* JSON, kept in one array of values and one text. Values point to each other
   by index, so there is one thing to free; index 0 is the document.
     struct json j = {0};
     json_result r = json_parse(&j, text);
     if (!r.ok) ... r.err.at ...              where it stopped
     size_t name = json_get(&j, 0, STR("name"));     0 if it is not there
     struct str s = json_text(&j, name);
     json_free(&j); */
#ifndef SAFEC_JSON_H
#define SAFEC_JSON_H
#include "text.h"
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

enum json_kind { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_TEXT, JSON_ARRAY, JSON_OBJECT };
struct json_span { size_t at, len; };
struct json_value {
  struct json_span key; /* in an object */
  size_t next;          /* the next one in the array or object, 0 at the end */
  enum json_kind kind;
  union _Tagged_by(kind) {
    int none _When(JSON_NULL);
    int truth _When(JSON_BOOL);
    double number _When(JSON_NUMBER);
    struct json_span text _When(JSON_TEXT);
    size_t first _When(JSON_ARRAY, JSON_OBJECT); /* 0 if it is empty */
  };
};
#if defined(__clang__)
#pragma clang assume_nonnull end /* vec.h has its own */
#endif
#define VEC_NAME json_values
#define VEC_TYPE struct json_value
#include "vec.h"
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif
struct json {
  struct json_values values;
  struct text text;
};

static inline void json_free(struct json *_Empties j) {
  json_values_free(&j->values);
  text_free(&j->text);
}
static inline struct json_value json_at(const struct json *j, size_t i) {
  struct json_value v = {.kind = JSON_NULL, .none = 0};
  json_values_get(&j->values, i, &v);
  return v;
}
static inline struct str json_key(const struct json *j, size_t i) {
  struct json_value v = json_at(j, i);
  return str_part(text_str(&j->text), v.key.at, v.key.len);
}
/* A text's bytes, or nothing if it is not one. */
static inline struct str json_text(const struct json *j, size_t i) {
  struct json_value v = json_at(j, i);
  struct json_span s = {0, 0};
  if (v.kind == JSON_TEXT)
    s = v.text;
  return str_part(text_str(&j->text), s.at, s.len);
}
static inline double json_number(const struct json *j, size_t i) {
  struct json_value v = json_at(j, i);
  return v.kind == JSON_NUMBER ? v.number : 0;
}
/* The first value in an array or object, then json_at(j, i).next. */
static inline size_t json_first(const struct json *j, size_t i) {
  struct json_value v = json_at(j, i);
  if (v.kind == JSON_ARRAY || v.kind == JSON_OBJECT)
    return v.first;
  return 0;
}
static inline size_t json_get(const struct json *j, size_t i, struct str key) {
  if (json_at(j, i).kind != JSON_OBJECT)
    return 0;
  for (size_t v = json_first(j, i); v; v = json_at(j, v).next)
    if (str_eq(json_key(j, v), key))
      return v;
  return 0;
}

/* Parsing: s is the input, *at where we are. Each returns 0 if it is not JSON. */
static inline int json_peek_(struct str s, const size_t *at) {
  return *at < s.len ? (unsigned char)s.p[*at] : -1;
}
static inline void json_space_(struct str s, size_t *at) {
  int c = json_peek_(s, at);
  while (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
    (*at)++;
    c = json_peek_(s, at);
  }
}
static inline int json_word_(struct str s, size_t *at, struct str word) {
  if (!str_eq(str_part(s, *at, word.len), word))
    return 0;
  *at += word.len;
  return 1;
}
static inline int json_digits_(struct str s, size_t *at) {
  size_t from = *at;
  while (json_peek_(s, at) >= '0' && json_peek_(s, at) <= '9')
    (*at)++;
  return *at > from;
}
static inline int json_number_(struct str s, size_t *at, double *out) {
  size_t from = *at;
  if (json_peek_(s, at) == '-')
    (*at)++;
  if (json_peek_(s, at) == '0')
    (*at)++;
  else if (!json_digits_(s, at))
    return 0;
  if (json_peek_(s, at) == '.') {
    (*at)++;
    if (!json_digits_(s, at))
      return 0;
  }
  if (json_peek_(s, at) == 'e' || json_peek_(s, at) == 'E') {
    (*at)++;
    if (json_peek_(s, at) == '+' || json_peek_(s, at) == '-')
      (*at)++;
    if (!json_digits_(s, at))
      return 0;
  }
  struct text t = {0};
  struct str n = str_part(s, from, *at - from);
  int ok = text_add(&t, n.p, n.len) && text_add(&t, "\0", 1) && t.p;
  if (ok && t.p)
    *out = strtod(t.p, NULL);
  text_free(&t);
  return ok;
}
static inline int json_hex_(struct str s, size_t *at, unsigned long *out) {
  unsigned long v = 0;
  for (int i = 0; i < 4; i++) {
    int c = json_peek_(s, at);
    (*at)++;
    if (c >= '0' && c <= '9')
      v = v * 16 + (unsigned long)(c - '0');
    else if (c >= 'a' && c <= 'f')
      v = v * 16 + (unsigned long)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      v = v * 16 + (unsigned long)(c - 'A' + 10);
    else
      return 0;
  }
  *out = v;
  return 1;
}
/* UTF-8. */
static inline int json_rune_(struct text *t, unsigned long r) {
  char b[4] = {0, 0, 0, 0};
  size_t n = r < 0x80 ? 1 : r < 0x800 ? 2 : r < 0x10000 ? 3 : 4;
  for (size_t i = n; i-- > 1; r >>= 6)
    b[i] = (char)(0x80 | (r & 0x3f));
  b[0] = (char)(n == 1 ? r : n == 2 ? 0xc0 | r : n == 3 ? 0xe0 | r : 0xf0 | r);
  return text_add(t, b, n);
}
static inline int json_string_(struct json *j, struct str s, size_t *at, struct json_span *out) {
  if (json_peek_(s, at) != '"')
    return 0;
  (*at)++;
  size_t from = j->text.len;
  for (;;) {
    int c = json_peek_(s, at);
    if (c < 0x20) /* the end, or a control character */
      return 0;
    (*at)++;
    if (c == '"')
      break;
    char one = (char)c;
    if (c == '\\') {
      c = json_peek_(s, at);
      if (c < 0)
        return 0;
      (*at)++;
      if (c == 'u') {
        unsigned long r = 0, low = 0;
        if (!json_hex_(s, at, &r))
          return 0;
        if (r >= 0xdc00 && r < 0xe000)
          return 0; /* half of a pair */
        if (r >= 0xd800 && r < 0xdc00) {
          if (!json_word_(s, at, STR("\\u")) || !json_hex_(s, at, &low) || low < 0xdc00 || low >= 0xe000)
            return 0;
          r = 0x10000 + ((r - 0xd800) << 10) + (low - 0xdc00);
        }
        if (!json_rune_(&j->text, r))
          return 0;
        continue;
      }
      size_t e = str_find(STR("\"\\/bfnrt"), (char)c);
      if (c == 0 || e == 8)
        return 0;
      one = str_part(STR("\"\\/\b\f\n\r\t"), e, 1).p[0];
    }
    if (!text_add(&j->text, &one, 1))
      return 0;
  }
  out->at = from;
  out->len = j->text.len - from;
  return 1;
}
static inline int json_value_(struct json *j, struct str s, size_t *at, struct json_span key, int depth, size_t *out);

/* What is in an array or object, up to its close. */
static inline int json_items_(struct json *j, struct str s, size_t *at, int object, int depth, size_t *first) {
  int close = object ? '}' : ']';
  size_t last = 0;
  (*at)++;
  json_space_(s, at);
  if (json_peek_(s, at) == close) {
    (*at)++;
    return 1;
  }
  for (;;) {
    struct json_span key = {0, 0};
    size_t i = 0;
    json_space_(s, at);
    if (object) {
      if (!json_string_(j, s, at, &key))
        return 0;
      json_space_(s, at);
      if (json_peek_(s, at) != ':')
        return 0;
      (*at)++;
    }
    if (!json_value_(j, s, at, key, depth, &i))
      return 0;
    if (last) {
      struct json_value v = json_at(j, last);
      v.next = i;
      json_values_set(&j->values, last, v);
    } else {
      *first = i;
    }
    last = i;
    json_space_(s, at);
    int c = json_peek_(s, at);
    (*at)++;
    if (c == close)
      return 1;
    if (c != ',')
      return 0;
  }
}

static inline int json_value_(struct json *j, struct str s, size_t *at, struct json_span key, int depth, size_t *out) {
  struct json_value v = {.key = key, .kind = JSON_NULL, .none = 0};
  size_t i = j->values.len, first = 0;
  if (depth > 500 || !json_values_add(&j->values, v))
    return 0;
  json_space_(s, at);
  int c = json_peek_(s, at);
  if (c == '{' || c == '[') {
    if (!json_items_(j, s, at, c == '{', depth + 1, &first))
      return 0;
    if (c == '{') {
      v.kind = JSON_OBJECT;
      v.first = first;
    } else {
      v.kind = JSON_ARRAY;
      v.first = first;
    }
  } else if (c == '"') {
    struct json_span t = {0, 0};
    if (!json_string_(j, s, at, &t))
      return 0;
    v.kind = JSON_TEXT;
    v.text = t;
  } else if (c == 't' || c == 'f') {
    int yes = c == 't';
    if (!json_word_(s, at, yes ? STR("true") : STR("false")))
      return 0;
    v.kind = JSON_BOOL;
    v.truth = yes;
  } else if (c == 'n') {
    if (!json_word_(s, at, STR("null")))
      return 0;
  } else {
    double d = 0;
    if (!json_number_(s, at, &d))
      return 0;
    v.kind = JSON_NUMBER;
    v.number = d;
  }
  *out = i;
  return json_values_set(&j->values, i, v);
}

/* Not JSON (or no memory): the byte where it stopped. */
struct json_error { size_t at; };
typedef RESULT(json_result, size_t, struct json_error) json_result;

/* The value is how many values there are; on an error j is empty. */
static inline json_result json_parse(struct json *j, struct str s) {
  struct json_span none = {0, 0};
  size_t at = 0, root = 0;
  json_free(j);
  if (json_value_(j, s, &at, none, 0, &root)) {
    json_space_(s, &at);
    if (at == s.len)
      return OK(json_result, j->values.len);
  }
  json_free(j);
  struct json_error e = {at};
  return ERR(json_result, e);
}

#if defined(__clang__)
#pragma clang assume_nonnull end
#endif
#endif
