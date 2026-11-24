// Random small Safe C programs for differential testing (Safe C vs ASan).
// Each program is: safe filler, then one BORROW, a KILL that may end it, and a
// USE, each possibly wrapped in control flow. A program with no real bug is
// accepted and runs clean; a hole is an accepted program that ASan flags.
// usage: bun gen.ts <seed> <out.c>     env NOKNOWN=1 drops already-known holes
const seed = Number(process.argv[2] ?? 1);
const out = process.argv[3] ?? "p.c";
let st = (Math.imul(seed, 2654435761 | 0) + 1) & 0x7fffffff;
function rnd(n: number): number {
  st = (Math.imul(st, 1103515245) + 12345) & 0x7fffffff;
  return Math.floor((st / 0x80000000) * n);
}
const pick = <T>(xs: T[]): T => xs[rnd(xs.length)];
const noKnown = !!process.env.NOKNOWN;
const lvl2 = process.env.NOKNOWN === "2"; // also drop roots found in round 2 (holes 07, 08, 11, 13, 14)

const S = ["s1", "s2"];
const idx = () => pick(["0", "1", "k", "(k^1)", "(n&1)", "(n-1)"]);
const field = () => pick(["p", "a[0]", "a[1]"]);
// lvalue for an owned slot; some go through roots the checker must resolve
function slot(): string {
  const s = pick(S);
  const f = field();
  const forms = [`${s}.${f}`, `${s}.${f}`, `${s}.a[${idx()}]`];
  if (!lvl2) forms.push(`G.${f}`, `GA[${idx()}].${f}`);
  if (!noKnown) forms.push(`e->${f}`);
  return pick(forms);
}
function baseAddr(sl: string): string {
  const m = sl.match(/^(.*?)(\.|->)(p|a\[[^\]]+\])$/)!;
  return m[2] === "->" ? m[1] : `&${m[1]}`;
}

// Ops that end a borrow of `sl` (or of everything) when the program is wrong.
function kill(sl: string): string {
  const addr = baseAddr(sl);
  const opts = [
    `free(_Take(${sl}));`,
    `{ struct S tmp = {0}; tmp.p = _Take(${sl}); free(_Take(tmp.p)); }`,
    `if (n) goto L1; free(_Take(${sl})); L1: ;`,
    `{ int c = 0; while (c++ < n) { if (c == 1) continue; } free(_Take(${sl})); }`,
    `free(_Take(${sl})); ${sl} = malloc(sizeof(int));`,
    `clear2(${addr}, ${addr});`,
    `free(_Take(${slot()}));`,
    `sfree(${addr});`,
    `reslot(&${sl});`,
    `{ int *_Nullable o = realloc(_Take(${sl}), 128); ${sl} = o; }`,
    `if (n > 0) free(_Take(${sl}));`,
    `for (int i = 0; i < n; i++) { free(_Take(${sl})); }`,
    `swap(${addr}, &s2);`,
    `deep(3, ${addr});`,
    `cb_free(sfree, ${addr});`,
  ];
  if (!lvl2) opts.push(`drop_G();`, `drop_all();`, `{ struct S *_Exclusive x = ${addr}; free(_Take(x->p)); free(_Take(x->a[0])); free(_Take(x->a[1])); }`);
  if (!noKnown) opts.push(`fn(${addr});`, `ops.f(${addr});`, `{ int *_Nullable *_Exclusive ps = &${sl}; free(_Take(*ps)); }`);
  return pick(opts);
}

function borrow(q: string, sl: string): string {
  const addr = baseAddr(sl);
  const opts = [
    `${q} = ${sl};`,
    `{ int *_Nullable t = ${sl}; ${q} = t; }`,
    `${q} = n > 0 ? ${sl} : &gint;`,
    `{ int *_Nullable c = ${sl}; for (int i = 0; i < 2; i++) { if (c) ${q} = c; } }`,
    `{ struct V v = {${sl}}; struct V v2 = v; ${q} = v2.p; }`,
    `{ int *_Nullable arr[2] = {${sl}, &gint}; ${q} = arr[k]; }`,
    `${q} = (sink++, ${sl});`,
    `{ struct S *c = n > 0 ? ${addr} : &s2; ${q} = c->${sl.replace(/^.*?(\.|->)/, "")}; }`,
    `{ struct S *c = ${addr}; for (int i = 0; i < 2; i++) c = ${addr}; ${q} = c->${sl.replace(/^.*?(\.|->)/, "")}; }`,
    `{ struct V v = {NULL}; v.p = ${sl}; ${q} = id_v(v).p; }`,
  ];
  // Borrows stored through another argument, and views in the heap.
  const thru = [
    `{ setp(&oq, ${sl}); ${q} = oq; }`,
    `{ setif(n, &oq, ${sl}); ${q} = oq; }`,
    `{ cb_set(setp, &oq, ${sl}); ${q} = oq; }`,
    `{ relay(&oq, ${sl}); ${q} = oq; }`,
    `{ peek(&oq, ${sl}); ${q} = oq; }`,
    `{ struct V v = {${sl}}, w = {NULL}; setv(&w, v); ${q} = w.p; }`,
    `{ struct V v = {${sl}}; copyv(&w0, &v); ${q} = w0.p; }`,
    `{ setslot(&oq, ${addr}); ${q} = oq; }`,
    `{ if (vt) { vt[${pick(["0", "1", "k"])}].p = ${sl}; ${q} = vt[k].p; } }`,
    `{ struct V v = {${sl}}; if (vt) { vt[0] = v; ${q} = vt[k].p; } }`,
    `{ struct V v = {${sl}}; if (vt) { *vt = v; v = vt[0]; ${q} = v.p; } }`,
    `{ struct V v = {${sl}}; if (vt) { setv(vt, v); ${q} = vt[0].p; } }`,
    `{ struct V v = {${sl}}; if (vt) { setv(&vt[k], v); ${q} = vt[k].p; } }`,
    `{ struct V v = {${sl}}; struct V *va = vt; if (va) { setv(va, v); ${q} = vt[0].p; } }`,
    `{ struct V v = {${sl}}; if (vt) { memcpy(vt, &v, sizeof v); ${q} = vt[0].p; } }`,
    `{ struct V v = {${sl}}; struct V *va = vt; if (va) { va[0] = v; ${q} = vt[0].p; } }`,
  ];
  if (rnd(2)) return pick(thru);
  if (!lvl2) opts.push(`${q} = getp(${addr});`, `{ struct V v = mkview(${addr}); ${q} = v.p; }`, `${q} = first(${addr}, &s2);`);
  if (!noKnown) opts.push(`${q} = get_G();`, `${q} = gA();`);
  return pick(opts);
}

function wrap(s: string, d = 0): string {
  if (d > 1) return s;
  switch (rnd(6)) {
    case 0: return `if (${pick(["n", "k == 0", "n > 0", "sink >= 0"])}) { ${wrap(s, d + 1)} }`;
    case 1: return `for (int i${d} = 0; i${d} < ${1 + rnd(2)}; i${d}++) { ${wrap(s, d + 1)} }`;
    case 2: return `switch (n) { case 1: ${wrap(s, d + 1)} break; default: ${s} break; }`;
    case 3: return `do { ${wrap(s, d + 1)} } while (0);`;
    default: return s;
  }
}

function filler(): string {
  return pick([
    `sink += n;`,
    `if (${slot()}) sink += 1;`,
    `t0.k = I; t0.i = ${rnd(9)};`,
    `t0.k = P; t0.p = &gint;`,
    `{ int *_Nullable z = malloc(sizeof(int)); if (z) { *z = 0x55; sink += *z; } free(z); }`,
    `if (!G.p) G.p = malloc(sizeof(int));`,
    `sp = &s${1 + rnd(2)};`,
  ]);
}

// Tagged-union triple: narrow, maybe change the tag, then read p.
function tagged(): string {
  const t = pick(["t0", "t1", "TG", `TA[${idx()}]`, "o.in", ...(lvl2 ? [] : []), "tt[0]", "tt[1]"]);
  const muts = [
    `${t}.k = I; ${t}.i = 0x41414140;`,
    `retag(&${t});`,
    `o = obad;`,
    `${t} = tbad;`,
    `sink += 0;`,
  ];
  if (!lvl2) muts.push(`glob_retag();`, `{ struct T *_Exclusive x = &${t}; x->k = I; x->i = 0x41414140; }`);
  if (!noKnown) muts.push(`TA[${idx()}].k = I; TA[${idx()}].i = 0x41414140;`, `${t}.k = P;`);
  return `if (${t}.k == P) { ${wrap(pick(muts))} ${wrap(`sink += *${t}.p;`)} }`;
}

const parts: string[] = [];
for (let i = rnd(3); i > 0; i--) parts.push(filler());
if (rnd(4) === 0) {
  parts.push(tagged());
} else {
  const sl = slot();
  parts.push(wrap(borrow("q1", sl)));
  for (let i = rnd(2); i > 0; i--) parts.push(filler());
  parts.push(wrap(kill(sl)));
  // refill freed blocks so a stale read sees 0x77 instead of the old value
  parts.push(`{ int *_Nullable sp1 = malloc(sizeof(int)); int *_Nullable sp2 = malloc(sizeof(int)); if (sp1) *sp1 = 0x77; if (sp2) *sp2 = 0x77; free(sp1); free(sp2); }`);
  parts.push(wrap(`if (q1) { sink += *q1; *q1 = 1; }`));
}
const body = parts.join("\n  ");

const prog = `#include <stdlib.h>
#include <stdio.h>
#include <string.h>
enum K { I, P };
struct T { enum K k; union _Tagged_by(k) { long i _When(I); int *_Static p _When(P); }; };
struct O { struct T in; };
struct S { int *_Owned _Nullable p; int *_Owned _Nullable a[2]; int v; };
struct _View V { int *_Nullable p; };
static _Thread_local int gint = 7;
static _Thread_local int sink;
static _Thread_local struct S G;
static _Thread_local struct S GA[2];
static _Thread_local struct T TG = {.k = P};
static _Thread_local struct T TA[2];
static int *_Nullable get_G(void) { return G.p; }
static int *_Nullable gA(void) { return GA[0].p; }
static void drop_G(void) { free(_Take(G.p)); free(_Take(G.a[0])); free(_Take(G.a[1])); }
static void drop_all(void) { drop_G(); for (int i = 0; i < 2; i++) { free(_Take(GA[i].p)); free(_Take(GA[i].a[0])); free(_Take(GA[i].a[1])); } }
static int *_Nullable getp(struct S *s _Lifetimebound) { return s->p; }
static int *_Nullable first(struct S *a _Lifetimebound, struct S *b) { (void)b; return a->p; }
static void sfree(struct S *_Exclusive s) { free(_Take(s->p)); free(_Take(s->a[0])); free(_Take(s->a[1])); }
static void deep(int d, struct S *_Exclusive s) { if (d == 0) sfree(s); else deep(d - 1, s); }
static void cb_free(void (*f)(struct S *_Exclusive), struct S *_Exclusive s) { f(s); }
static void swap(struct S *_Exclusive a, struct S *_Exclusive b) { int *_Nullable t = _Take(a->p); a->p = _Take(b->p); b->p = t; free(_Take(b->p)); }
static void reslot(int *_Owned _Nullable *_Exclusive s) { free(_Take(*s)); *s = malloc(sizeof(int)); }
static struct V id_v(struct V v _Lifetimebound) { return v; }
static void clear2(struct S *_Exclusive a, struct S *_Exclusive b) { (void)b; free(_Take(a->p)); free(_Take(a->a[0])); free(_Take(a->a[1])); }
static struct V mkview(struct S *s _Lifetimebound) { struct V v = {s->p}; return v; }
static void setp(int *_Nullable __single *out, int *_Nullable in) { *out = in; }
static void setif(int n, int *_Nullable __single *out, int *_Nullable in) { if (n > 0) *out = in; else *out = NULL; }
static void relay(int *_Nullable __single *out, int *_Nullable in) { setp(out, in); }
static void peek(int *_Nullable __single *out, int *_Nullable in _Noescape) { *out = NULL; if (in) sink += *in; }
static void cb_set(void (*f)(int *_Nullable __single *, int *_Nullable), int *_Nullable __single *out, int *_Nullable in) { f(out, in); }
static void setv(struct V *out, struct V in) { *out = in; }
static void copyv(struct V *out, struct V *in) { *out = *in; }
static void setslot(int *_Nullable __single *out, struct S *s) { *out = s->p; }
static void retag(struct T *_Exclusive t) { t->k = I; t->i = 0x41414140; }
static void glob_retag(void) { TG.k = I; TG.i = 0x41414140; { struct T *a0 = &TA[0]; a0->k = I; a0->i = 0x41414140; } }
struct Ops { void (*f)(struct S *_Exclusive); };
static void fill(struct S *_Exclusive s) { s->p = malloc(sizeof(int)); s->a[0] = malloc(sizeof(int)); s->a[1] = malloc(sizeof(int)); if (s->p) *s->p = 1; }
int main(int argc, char **argv) {
  (void)argv;
  int k = argc - 1, n = argc;
  struct S s1 = {0}, s2 = {0};
  fill(&s1); fill(&s2); fill(&G); fill(&GA[0]); fill(&GA[1]);
  struct S *sp = &s1;
  struct S *_Exclusive e = &s1;
  void (*fn)(struct S *_Exclusive) = sfree;
  struct Ops ops = {sfree};
  struct T t0 = {.k = P}, t1 = {.k = P}, tbad = {.k = I};
  t0.p = &gint; t1.p = &gint; TG.k = P; TG.p = &gint; tbad.i = 0x41414140;
  struct O o = {{.k = P}}, obad = {{.k = I}};
  o.in.k = P; o.in.p = &gint; obad.in.k = I; obad.in.i = 0x41414140;
  { struct T *a0 = &TA[0], *a1 = &TA[1]; a0->k = P; a0->p = &gint; a1->k = P; a1->p = &gint; }
  struct T tt[2] = {{.k = P}, {.k = P}};
  { struct T *b0 = &tt[0], *b1 = &tt[1]; b0->k = P; b0->p = &gint; b1->k = P; b1->p = &gint; }
  int *_Nullable q1 = NULL;
  int *_Nullable __single oq = NULL;
  struct V w0 = {NULL};
  struct V *_Owned _Nullable vt = calloc(2, sizeof(struct V));
  ${body}
  sfree(&s1); sfree(&s2); drop_all();
  free(vt); (void)oq; (void)w0;
  (void)q1; (void)e; (void)fn; (void)ops; (void)sp;
  printf("%d\\n", sink);
  return 0;
}
`;
await Bun.write(out, prog);
