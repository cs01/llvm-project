// A tour of Safe C for Rust programmers. Compiles and runs under -fsafe-c,
// and as plain C with any compiler (the annotations vanish).
#include <stdio.h>

#include "../safec_std.h"

// enum Shape { Circle(f64), Rect { w: f64, h: f64 } }
enum shape_kind { CIRCLE, RECT };
struct shape {
  enum shape_kind kind;
  union _Tagged_by(kind) {
    double radius _When(CIRCLE);
    struct { double w, h; } rect _When(RECT);
  };
};

// match shape { ... }: every variant must be handled.
static double area(const struct shape *s) {
  switch (s->kind) {
  case CIRCLE:
    return 3.14159 * s->radius * s->radius;
  case RECT:
    return s->rect.w * s->rect.h;
  }
  return 0;
}

// struct Node { v: i32, next: Option<Box<Node>> }
struct node {
  int v;
  struct node *_Owned _Nullable next;
};

struct list {
  struct node *_Owned _Nullable head;
};

// fn push(&mut self, v: i32)
static bool push(struct list *_Exclusive l, int v) {
  struct node *_Nullable n = calloc(1, sizeof(struct node));
  if (!n)
    return false;
  n->v = v;
  n->next = _Take(l->head); // Option::take
  l->head = n;              // moves n into the list
  return true;
}

// impl Drop for List
static void free_list(struct list *_Exclusive l) {
  struct node *_Nullable n = _Take(l->head);
  while (n) {
    struct node *_Nullable next = _Take(n->next);
    free(n);
    n = next;
  }
}

// fn sum_line(line: &str) -> Result<i64, ParseError>, using `?`
static i64_result sum_line(str line) {
  int64_t total = 0;
  str part = S("");
  while (str_split(&line, ',', &part))
    total += TRY(str_to_i64(str_trim(part)), i64_result);
  return OK(i64_result, total);
}

// Vec<i32>: a VEC type stamped out once, with its Drop.
typedef VEC(int) int_vec;
static void int_vec_free(int_vec *_Exclusive v) { vec_free(v); }

int main(void) {
  struct shape shapes[2] = {{.kind = CIRCLE, .radius = 1.0},
                            {.kind = RECT, .rect = {2, 3}}};
  for (int i = 0; i < 2; i++)
    printf("area %d: %.2f\n", i, area(&shapes[i]));

  struct list list _Drop(free_list) = {NULL};
  for (int i = 1; i <= 3; i++)
    push(&list, i);
  for (const struct node *_Nullable n = list.head; n; n = n->next)
    printf("node %d\n", n->v);

  // Vec<i32>, freed at the end of the scope like a Rust Drop
  {
    int_vec nums _Drop(int_vec_free) = {0, 0, 0};
    for (int i = 0; i < 5; i++)
      vec_push(&nums, i * i);
    vec_each(int x, nums) printf("square %d\n", x);
  }

  i64_result r = sum_line(S("1, 2, 3"));
  if (r.ok)
    printf("sum %lld\n", (long long)r.value);
  i64_result bad = sum_line(S("1, 2x, 3"));
  if (!bad.ok && bad.err.kind == PARSE_BAD_CHAR)
    printf("bad char '%c' at %zu\n", bad.err.bad.c, bad.err.bad.at);
  return 0;
}
