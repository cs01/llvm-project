/* tsort [file]: ported from suckless sbase (MIT). The graph's pointers are indexes; 0 is none. */
#include "../args.h"
#include "../vec.h"
#include <ctype.h>

struct vertex { size_t name, len, next, edges, in; int colour; };
struct edge { size_t to, next; };
VEC(vertices, struct vertex);
VEC(edges, struct edge);
struct graph { struct vertices v; struct edges e; struct text names; };
enum { WHITE, GREY, BLACK };

static struct vertex vertex(const struct graph *g, size_t i) {
  struct vertex v = {0, 0, 0, 0, 0, BLACK};
  vertices_get(&g->v, i, &v);
  return v;
}
static struct edge edge(const struct graph *g, size_t i) {
  struct edge e = {0, 0};
  edges_get(&g->e, i, &e);
  return e;
}
static struct str name(const struct graph *g, size_t i) {
  struct vertex v = vertex(g, i);
  return str_part(text_str(&g->names), v.name, v.len);
}

/* The vertex called s, in the list by name; 0 if there is no memory. */
static size_t add_vertex(struct graph *g, struct str s) {
  size_t before = 0;
  for (size_t i = vertex(g, 0).next; i; i = vertex(g, i).next) {
    int c = str_cmp(s, name(g, i));
    if (c == 0)
      return i;
    if (c < 0)
      break;
    before = i;
  }
  struct vertex v = {g->names.len, s.len, vertex(g, before).next, 0, 0, WHITE};
  size_t at = g->v.len;
  if (!text_add(&g->names, s.p, s.len) || !vertices_add(&g->v, v))
    return 0;
  struct vertex b = vertex(g, before);
  b.next = at;
  vertices_set(&g->v, before, b);
  return at;
}

static int add_edge(struct graph *g, size_t from, size_t to) {
  size_t before = 0;
  for (size_t i = vertex(g, from).edges; i; i = edge(g, i).next) {
    if (edge(g, i).to == to)
      return 1;
    if (str_cmp(name(g, to), name(g, edge(g, i).to)) < 0)
      break;
    before = i;
  }
  struct vertex f = vertex(g, from);
  struct edge b = edge(g, before);
  struct edge e = {to, before ? b.next : f.edges};
  size_t at = g->e.len;
  if (!edges_add(&g->e, e))
    return 0;
  if (before) {
    b.next = at;
    edges_set(&g->e, before, b);
  } else {
    f.edges = at;
    vertices_set(&g->v, from, f);
  }
  struct vertex t = vertex(g, to);
  t.in++;
  vertices_set(&g->v, to, t);
  return 1;
}

static int load(struct graph *g, FILE *f) {
  struct text line = {0};
  size_t from = 0;
  int ok = 1;
  while (ok && text_line(&line, f)) {
    struct str rest = str_trim(text_str(&line));
    while (ok && rest.len) {
      size_t n = 0;
      while (n < rest.len && !isspace((unsigned char)rest.p[n]))
        n++;
      size_t v = add_vertex(g, str_part(rest, 0, n));
      ok = v != 0 && (!from || from == v || add_edge(g, from, v));
      from = from ? 0 : v;
      rest = str_trim(str_part(rest, n, rest.len));
    }
  }
  text_free(&line);
  if (from)
    fputs("odd number of tokens in input\n", stderr);
  return ok && !from;
}

static int visit(struct graph *g, size_t u) {
  int loop = 0;
  struct vertex x = vertex(g, u);
  x.colour = GREY;
  vertices_set(&g->v, u, x);
  struct str s = name(g, u);
  fwrite(s.p, 1, s.len, stdout);
  putchar('\n');
  for (size_t i = x.edges; i; i = edge(g, i).next) {
    size_t to = edge(g, i).to;
    struct vertex v = vertex(g, to);
    if (v.colour == WHITE) {
      v.in--;
      vertices_set(&g->v, to, v);
      if (v.in == 0)
        loop |= visit(g, to);
    } else if (v.colour == GREY) {
      loop = 1;
      fputs("loop detected\n", stderr);
    }
  }
  x = vertex(g, u);
  x.colour = BLACK;
  vertices_set(&g->v, u, x);
  return loop;
}

static int sort(struct graph *g) {
  int loop = 0;
  for (size_t in = 0; vertex(g, 0).next; in++) {
    size_t before = 0;
    for (size_t u = vertex(g, 0).next; u; u = vertex(g, u).next) {
      if (vertex(g, u).colour == WHITE && vertex(g, u).in > in) {
        before = u;
        continue;
      }
      if (vertex(g, u).colour == WHITE)
        loop |= visit(g, u);
      struct vertex b = vertex(g, before);
      b.next = vertex(g, u).next;
      vertices_set(&g->v, before, b);
    }
  }
  return loop;
}

int main(int argc, char *__null_terminated *__counted_by(argc) argv) {
  struct graph g = {0};
  struct vertex head = {0, 0, 0, 0, 0, BLACK};
  struct edge none = {0, 0};
  int ok = vertices_add(&g.v, head) && edges_add(&g.e, none);
  if (argc > 1 && strcmp(argv[1], "-") != 0) {
    FILE *_Owned _Nullable f = fopen(argv[1], "r");
    if (!f)
      return perror(argv[1]), 2;
    ok = ok && load(&g, f);
    fclose(f);
  } else {
    ok = ok && load(&g, stdin);
  }
  int r = ok ? sort(&g) : 2;
  vertices_free(&g.v);
  edges_free(&g.e);
  text_free(&g.names);
  return r;
}
