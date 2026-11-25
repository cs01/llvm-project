// parson's object/array/dotted path/deep copy/parse core in the core subset: no __unsafe, no borrow
// stored anywhere. Differences from parson: keys are counted strings, the union is three owner fields,
// no parent pointers, the parse cursor is an index. Allocation failure is handled.
#include <stdio.h>
#include <stdlib.h>
#define NONE ((size_t)-1)
typedef struct str { size_t n; char *_Owned _Nullable __counted_by(n) s; } Str;
typedef struct value { int type; double num; struct str *_Owned _Nullable str; struct object *_Owned _Nullable obj; struct array *_Owned _Nullable arr; } Value;
typedef struct item { struct str *_Owned _Nullable name; struct value *_Owned _Nullable v; unsigned long hash; size_t cell; } Item;
typedef struct object { size_t ncells; size_t *_Owned _Nullable __counted_by(ncells) cells; size_t cap; struct item *_Owned _Nullable __counted_by(cap) items; size_t count; } Object;
typedef struct array { size_t cap; struct value *_Owned _Nullable *_Owned _Nullable __counted_by(cap) items; size_t count; } Array;
enum { T_NULL, T_NUM, T_STR, T_OBJ, T_ARR };
void value_free(Value *_Owned _Nullable v);

Str *_Owned _Nullable str_make(const char *__counted_by(n) s, size_t n) {
  Str *_Owned _Nullable r = calloc(1, sizeof *r);
  if (!r) return NULL;
  char *_Owned _Nullable b = calloc(n + 1, 1);
  if (!b) { free(r); return NULL; }
  for (size_t i = 0; i < n; i++) b[i] = s[i];
  r->s = b; r->n = n;
  return r;
}
void str_free(Str *_Owned _Nullable s) { if (!s) return; free(_Take(s->s)); free(s); }
_Bool str_is(const Str *a, const char *__counted_by(n) s, size_t n) {
  if (a->n != n || !a->s) return n == 0 && a->n == 0;
  for (size_t i = 0; i < n; i++) if (a->s[i] != s[i]) return 0;
  return 1;
}
unsigned long hash_of(const char *__counted_by(n) s, size_t n) { unsigned long h = 5381; for (size_t i = 0; i < n; i++) h = h * 33u + (unsigned char)s[i]; return h; }

// split from its null check, here and below: clang crashes on a counted field indexed through a _Nullable pointer
void array_clear(Array *a) {
  if (a->items) for (size_t i = 0; i < a->cap; i++) value_free(_Take(a->items[i]));
  free(_Take(a->items));
}
void array_free(Array *_Owned _Nullable a) { if (!a) return; array_clear(a); free(a); }
void object_clear(Object *o) {
  if (o->items) for (size_t i = 0; i < o->cap; i++) { Item *it = &o->items[i]; str_free(_Take(it->name)); value_free(_Take(it->v)); }
  free(_Take(o->items)); free(_Take(o->cells));
}
void object_free(Object *_Owned _Nullable o) { if (!o) return; object_clear(o); free(o); }
void value_free(Value *_Owned _Nullable v) { if (!v) return; str_free(_Take(v->str)); object_free(_Take(v->obj)); array_free(_Take(v->arr)); free(v); }
Value *_Owned _Nullable value_new(int type) { Value *_Owned _Nullable v = calloc(1, sizeof *v); if (!v) return NULL; v->type = type; return v; }
Value *_Owned _Nullable value_num(double d) { Value *_Owned _Nullable v = value_new(T_NUM); if (v) v->num = d; return v; }
Value *_Owned _Nullable value_object(void) { Value *_Owned _Nullable v = value_new(T_OBJ); if (!v) return NULL; Object *_Owned _Nullable o = calloc(1, sizeof *o); if (!o) { value_free(v); return NULL; } v->obj = o; return v; }
Value *_Owned _Nullable value_array(void) { Value *_Owned _Nullable v = value_new(T_ARR); if (!v) return NULL; Array *_Owned _Nullable a = calloc(1, sizeof *a); if (!a) { value_free(v); return NULL; } v->arr = a; return v; }
// rule 9: one getter for reading and changing
Object *_Nullable value_get_object(const Value *_Nullable v) { return v ? v->obj : NULL; }
Array *_Nullable value_get_array(const Value *_Nullable v) { return v ? v->arr : NULL; }
double value_get_number(const Value *_Nullable v) { return v ? v->num : 0; }

size_t cell_ix(const Object *o, const char *__counted_by(n) key, size_t n, unsigned long h, _Bool *found) {
  *found = 0;
  if (!o->cells || !o->items) return NONE;
  for (size_t i = 0; i < o->ncells; i++) {
    size_t ix = (h + i) & (o->ncells - 1), c = o->cells[ix];
    if (c == NONE) return ix;
    const Item *it = &o->items[c];
    if (it->hash != h) continue;
    const Str *_Nullable k = it->name;
    if (k && str_is(k, key, n)) { *found = 1; return ix; }
  }
  return NONE;
}
Value *_Nullable object_find(const Object *o, const char *__counted_by(n) key, size_t n) {
  _Bool found; size_t c = cell_ix(o, key, n, hash_of(key, n), &found);
  if (!found || !o->cells || !o->items) return NULL;
  return o->items[o->cells[c]].v;
}
Value *_Nullable object_get(const Object *_Nullable o, const char *__counted_by(n) key, size_t n) { if (!o) return NULL; return object_find(o, key, n); }
const Item *_Nullable object_item(const Object *o, size_t i) { if (!o->items || i >= o->count) return NULL; return &o->items[i]; }
// moves every item into bigger arrays; nothing has moved if it fails
_Bool object_grow(Object *o) {
  size_t nc = o->ncells ? o->ncells * 2 : 16, cap = nc * 7 / 10;
  size_t *_Owned _Nullable cells = malloc(nc * sizeof *cells);
  if (!cells) return 0;
  Item *_Owned _Nullable items = calloc(cap, sizeof *items);
  if (!items) { free(cells); return 0; }
  for (size_t i = 0; i < nc; i++) cells[i] = NONE;
  if (o->items) for (size_t i = 0; i < o->count; i++) {
    Item *from = &o->items[i];
    unsigned long h = from->hash; size_t ix = h & (nc - 1);
    while (cells[ix] != NONE) ix = (ix + 1) & (nc - 1);
    cells[ix] = i;
    Item *to = &items[i];
    to->name = _Take(from->name); to->v = _Take(from->v); to->hash = h; to->cell = ix;
  }
  free(_Take(o->items)); free(_Take(o->cells));
  o->items = items; o->cap = cap; o->cells = cells; o->ncells = nc;
  return 1;
}
_Bool object_set(Object *o, const char *__counted_by(n) key, size_t n, Value *_Owned v) {
  unsigned long h = hash_of(key, n); _Bool found; size_t c = cell_ix(o, key, n, h, &found);
  if (found && o->cells && o->items) { Item *it = &o->items[o->cells[c]]; value_free(_Take(it->v)); it->v = v; return 1; }
  if (o->count >= o->cap) { if (!object_grow(o)) { value_free(v); return 0; } c = cell_ix(o, key, n, h, &found); }
  if (!o->cells || !o->items || c == NONE) { value_free(v); return 0; }
  Str *_Owned _Nullable k = str_make(key, n);
  if (!k) { value_free(v); return 0; }
  Item *it = &o->items[o->count];
  it->name = k; it->v = v; it->hash = h; it->cell = c;
  o->cells[c] = o->count++;
  return 1;
}
Value *_Owned _Nullable object_remove(Object *o, const char *__counted_by(n) key, size_t n) {
  _Bool found; size_t cell = cell_ix(o, key, n, hash_of(key, n), &found);
  if (!found || !o->cells || !o->items) return NULL;
  size_t ix = o->cells[cell], last = o->count - 1;
  Item *it = &o->items[ix];
  Value *_Owned _Nullable v = _Take(it->v);
  str_free(_Take(it->name));
  if (ix < last) {
    Item *l = &o->items[last];
    Str *_Owned _Nullable ln = _Take(l->name); Value *_Owned _Nullable lv = _Take(l->v); unsigned long lh = l->hash; size_t lc = l->cell;
    Item *to = &o->items[ix];
    to->name = ln; to->v = lv; to->hash = lh; to->cell = lc;
    o->cells[lc] = ix;
  }
  o->count--;
  size_t i = cell, j = cell;
  for (size_t x = 0; x + 1 < o->ncells; x++) {
    j = (j + 1) & (o->ncells - 1);
    if (o->cells[j] == NONE) break;
    Item *jt = &o->items[o->cells[j]];
    size_t k = jt->hash & (o->ncells - 1);
    if ((j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j))) { jt->cell = i; o->cells[i] = o->cells[j]; i = j; }
  }
  o->cells[i] = NONE;
  return v;
}
size_t dot(const char *__counted_by(n) s, size_t n) { for (size_t i = 0; i < n; i++) if (s[i] == '.') return i; return n; }
Value *_Nullable object_dotget(const Object *_Nullable o, const char *__counted_by(n) p, size_t n) {
  size_t d = dot(p, n);
  if (d == n) return object_get(o, p, n);
  return object_dotget(value_get_object(object_get(o, p, d)), p + d + 1, n - d - 1);
}
Value *_Owned _Nullable object_dotremove(Object *_Nullable o, const char *__counted_by(n) p, size_t n) {
  if (!o) return NULL;
  size_t d = dot(p, n);
  if (d == n) return object_remove(o, p, n);
  return object_dotremove(value_get_object(object_get(o, p, d)), p + d + 1, n - d - 1);
}
_Bool object_dotset(Object *o, const char *__counted_by(n) p, size_t n, Value *_Owned v) {
  size_t d = dot(p, n);
  if (d == n) return object_set(o, p, n, v);
  Value *_Nullable t = object_get(o, p, d);
  if (t) { Object *_Nullable in = value_get_object(t); if (!in) { value_free(v); return 0; } return object_dotset(in, p + d + 1, n - d - 1, v); }
  // the new object goes in first, so failure needs no undo of the caller's value
  Value *_Owned _Nullable nv = value_object();
  if (!nv) { value_free(v); return 0; }
  if (!object_set(o, p, d, nv)) { value_free(v); return 0; }
  Object *_Nullable in = value_get_object(object_get(o, p, d));
  if (!in) value_free(v);
  else if (object_dotset(in, p + d + 1, n - d - 1, v)) return 1;
  value_free(object_remove(o, p, d));
  return 0;
}
_Bool array_add(Array *a, Value *_Owned v) {
  if (a->count >= a->cap) {
    size_t cap = a->cap ? a->cap * 2 : 16;
    Value *_Owned _Nullable *_Owned _Nullable n = calloc(cap, sizeof *n);
    if (!n) { value_free(v); return 0; }
    if (a->items) for (size_t i = 0; i < a->count; i++) n[i] = _Take(a->items[i]);
    free(_Take(a->items));
    a->items = n; a->cap = cap;
  }
  if (!a->items) { value_free(v); return 0; }
  a->items[a->count] = v; a->count++;
  return 1;
}
Value *_Nullable array_at(const Array *a, size_t i) { if (!a->items || i >= a->count) return NULL; return a->items[i]; }
Value *_Nullable array_get(const Array *_Nullable a, size_t i) { if (!a) return NULL; return array_at(a, i); }
_Bool array_remove(Array *a, size_t ix) {
  if (!a->items || ix >= a->count) return 0;
  value_free(_Take(a->items[ix]));
  for (size_t i = ix; i + 1 < a->count; i++) a->items[i] = _Take(a->items[i + 1]);
  a->count--;
  return 1;
}
Str *_Owned _Nullable str_copy(const Str *s) { if (!s->s) return NULL; return str_make(s->s, s->n); }
_Bool object_set_str(Object *o, const Str *k, Value *_Owned v) { if (!k->s) { value_free(v); return 0; } return object_set(o, k->s, k->n, v); }
Value *_Owned _Nullable deep_copy(const Value *_Nullable v) {
  if (!v) return NULL;
  if (v->type == T_NUM) return value_num(v->num);
  if (v->type == T_STR) {
    Value *_Owned _Nullable r = value_new(T_STR); const Str *_Nullable s = v->str;
    if (!r) return NULL;
    if (s) r->str = str_copy(s);
    return r;
  }
  if (v->type == T_ARR) {
    Value *_Owned _Nullable r = value_array(); if (!r) return NULL;
    const Array *_Nullable a = v->arr; Array *_Nullable to = r->arr;
    if (a && to) for (size_t i = 0; i < a->count; i++) {
      Value *_Owned _Nullable c = deep_copy(array_get(a, i));
      if (!c) { value_free(r); return NULL; }
      if (!array_add(to, c)) { value_free(r); return NULL; }
    }
    return r;
  }
  if (v->type == T_OBJ) {
    Value *_Owned _Nullable r = value_object(); if (!r) return NULL;
    const Object *_Nullable o = v->obj; Object *_Nullable to = r->obj;
    if (o && to) for (size_t i = 0; i < o->count; i++) {
      const Item *_Nullable it = object_item(o, i); if (!it) { value_free(r); return NULL; }
      const Str *_Nullable k = it->name;
      Value *_Owned _Nullable c = deep_copy(it->v);
      if (!c) { value_free(r); return NULL; }
      if (!k) { value_free(c); value_free(r); return NULL; }
      if (!object_set_str(to, k, c)) { value_free(r); return NULL; }
    }
    return r;
  }
  return value_new(T_NULL);
}
int at(const char *__counted_by(n) s, size_t n, size_t p) { return p < n ? s[p] : 0; }
size_t ws(const char *__counted_by(n) s, size_t n, size_t p) { while (at(s, n, p) == ' ' || at(s, n, p) == '\n') p++; return p; }
size_t quoted(const char *__counted_by(n) s, size_t n, size_t p) { while (at(s, n, p) && at(s, n, p) != '"') p++; return p; }
Value *_Owned _Nullable parse(const char *__counted_by(n) s, size_t n, size_t *p, int depth) {
  if (depth > 100) return NULL;
  *p = ws(s, n, *p);
  int c = at(s, n, *p);
  if (c == '{') {
    Value *_Owned _Nullable r = value_object(); if (!r) return NULL;
    Object *_Nullable o = value_get_object(r);
    *p = ws(s, n, *p + 1);
    if (at(s, n, *p) == '}') { (*p)++; return r; }
    for (;;) {
      size_t a = *p + 1, b = quoted(s, n, a);
      *p = ws(s, n, b + 1);
      if (at(s, n, *p) != ':') { value_free(r); return NULL; }
      (*p)++;
      Value *_Owned _Nullable v = parse(s, n, p, depth + 1);
      if (!v) { value_free(r); return NULL; }
      if (!o) { value_free(v); value_free(r); return NULL; }
      if (!object_set(o, s + a, b - a, v)) { value_free(r); return NULL; }
      *p = ws(s, n, *p);
      if (at(s, n, *p) != ',') break;
      *p = ws(s, n, *p + 1);
    }
    if (at(s, n, *p) != '}') { value_free(r); return NULL; }
    (*p)++;
    return r;
  }
  if (c == '[') {
    Value *_Owned _Nullable r = value_array(); if (!r) return NULL;
    Array *_Nullable a = value_get_array(r);
    (*p)++;
    for (;;) {
      Value *_Owned _Nullable v = parse(s, n, p, depth + 1);
      if (!v) { value_free(r); return NULL; }
      if (!a) { value_free(v); value_free(r); return NULL; }
      if (!array_add(a, v)) { value_free(r); return NULL; }
      *p = ws(s, n, *p);
      if (at(s, n, *p) != ',') break;
      (*p)++;
    }
    if (at(s, n, *p) != ']') { value_free(r); return NULL; }
    (*p)++;
    return r;
  }
  if (c == '"') {
    size_t a = *p + 1, b = quoted(s, n, a);
    if (at(s, n, b) != '"') return NULL;
    Value *_Owned _Nullable r = value_new(T_STR); if (!r) return NULL;
    r->str = str_make(s + a, b - a); *p = b + 1;
    return r;
  }
  if (c < '0' || c > '9') return NULL;
  double d = 0;
  while (at(s, n, *p) >= '0' && at(s, n, *p) <= '9') { d = d * 10 + (at(s, n, *p) - '0'); (*p)++; }
  return value_num(d);
}
int key(char *__counted_by(8) b, int i) { b[0] = 'k'; int n = 1; if (i >= 100) b[n++] = (char)('0' + i / 100); if (i >= 10) b[n++] = (char)('0' + i / 10 % 10); b[n++] = (char)('0' + i % 10); return n; }
int main(void) {
  const char src[] = "{\"a\":{\"b\":[1,2,{\"c\":3}]},\"s\":\"x\"}";
  size_t p = 0;
  Value *_Owned _Nullable v = parse(src, sizeof src - 1, &p, 0);
  if (!v) return 1;
  Value *_Owned _Nullable copy = deep_copy(v);
  Object *_Nullable o = value_get_object(v);
  if (!copy || !o) return 2;
  char b[8] = {0};
  for (int i = 0; i < 200; i++) { int n = key(b, i); Value *_Owned _Nullable x = value_num(i); if (!x) return 3; if (!object_set(o, b, (size_t)n, x)) return 3; }
  for (int i = 0; i < 200; i += 2) { int n = key(b, i); Value *_Owned _Nullable x = object_remove(o, b, (size_t)n); if (!x) return 4; value_free(x); }
  for (int i = 0; i < 200; i++) { int n = key(b, i); const Value *_Nullable got = object_get(o, b, (size_t)n); if ((i % 2 == 1) != (got != NULL)) return 5; if (got && value_get_number(got) != i) return 6; }
  Value *_Owned _Nullable seven = value_num(7); if (!seven) return 7;
  if (!object_dotset(o, "x.y.z", 5, seven)) return 7;
  if (value_get_number(object_dotget(o, "x.y.z", 5)) != 7) return 8;
  Value *_Owned _Nullable one = value_num(1); if (!one) return 9;
  if (object_dotset(o, "s.q", 3, one)) return 9;
  Value *_Owned _Nullable gone = object_dotremove(o, "x.y.z", 5); if (!gone) return 10; value_free(gone);
  Array *_Nullable a = value_get_array(object_dotget(o, "a.b", 3));
  if (!a || !array_remove(a, 0) || a->count != 2) return 11;
  if (value_get_number(array_get(a, 0)) != 2) return 12;
  if (value_get_number(array_get(value_get_array(object_dotget(value_get_object(copy), "a.b", 3)), 0)) != 1) return 13;
  value_free(copy); value_free(v);
  printf("ok\n");
  return 0;
}
