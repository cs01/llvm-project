// Diff two clang-semindex outputs: the names an edit touched, or why it must
// be treated as affecting every dependent.
//
// An entity whose own tokens changed is changed, and so is every entity that
// refers to a changed one, followed through the reference graph of both
// versions: `constexpr int B = A + 1` changes when only A was edited. Bodies
// are separate nodes ("f()"); nothing refers to a body, so a body change stays
// with the compiles that load that body.
import { readFileSync } from "fs";

type Ent = { key: string; names: string[]; nameless: boolean; body: boolean; hash: string };
type GraphNode = { refs: string[]; names: string[]; body?: string };
export type Idx = {
  found: boolean; errors: boolean; counter: boolean; rest: string;
  macros: Record<string, string>; entities: Ent[]; graph: Record<string, GraphNode>;
  conditionMacros?: string[];
};
export type Diff = { all: string | null; names: Set<string>; bodies: Set<string>; changedKeys: string[] };

// "clang::FunctionDecl::isVariadic#CXXMethod#body" -> "clang::FunctionDecl::isVariadic()"
const nodeOf = (e: Ent) => e.key.split("#")[0] + (e.body ? "()" : "");
const lastName = (q: string) => q.replace(/\(\)$/, "").split("::").pop()!;

export function semdiff(a: Idx, b: Idx): Diff {
  const names = new Set<string>(), bodies = new Set<string>();
  const none = (all: string): Diff => ({ all, names, bodies, changedKeys: [] });
  if (!a.found || !b.found) return none("header not found in the indexing TU");
  if (a.errors || b.errors) return none("header did not parse cleanly");
  if (a.counter || b.counter) return none("__COUNTER__ is used");
  if (a.rest !== b.rest) return none("tokens outside any named entity changed");

  // A changed macro is a node ("%NAME") like any entity, except that a macro
  // tested by a conditional decides which declarations exist at all.
  const changed = new Set<string>();
  const tested = new Set([...(a.conditionMacros ?? []), ...(b.conditionMacros ?? [])]);
  // Keys are "#undef NAME" or a definition, "NAME@<file>#<n>"; compiles record
  // the macros they use by name.
  for (const k of new Set([...Object.keys(a.macros), ...Object.keys(b.macros)])) {
    if (a.macros[k] === b.macros[k]) continue;
    const m = k.replace(/^#undef /, "").replace(/@.*$/, "");
    if (tested.has(m)) return none(`macro ${m} changed and is tested by a conditional`);
    // An #undef moved or appeared changes which definition later expansions
    // see, which the definitions' own keys do not show.
    if (k.startsWith("#undef ")) return none(`#undef ${m} changed`);
    changed.add(`%${k}`);
    names.add(`%${m}`);
  }

  // Overloads share a key, so compare each key's multiset of hashes.
  const group = (x: Idx) => {
    const m = new Map<string, Ent[]>();
    for (const e of x.entities) m.set(e.key, [...(m.get(e.key) ?? []), e]);
    return m;
  };
  const ga = group(a), gb = group(b);
  const changedKeys: string[] = [];
  // Entities added or removed: only these reach uses through a pointer or
  // reference ("&name"), which depend on existence, not on the definition.
  const appeared = new Set<string>();
  for (const key of new Set([...ga.keys(), ...gb.keys()])) {
    const ea = ga.get(key) ?? [], eb = gb.get(key) ?? [];
    const ha = ea.map((e) => e.hash).sort().join(), hb = eb.map((e) => e.hash).sort().join();
    if (ha === hb) continue;
    changedKeys.push(key);
    for (const e of [...ea, ...eb]) {
      if (e.nameless) return { all: `nameless entity ${key} changed`, names, bodies, changedKeys };
      changed.add(nodeOf(e));
      if (!ea.length || !eb.length) appeared.add(nodeOf(e));
      if (e.body) bodies.add(e.names[0]);
      else e.names.forEach((n) => names.add(n));
    }
  }

  // Follow references backwards to everything that uses a changed entity.
  const users = new Map<string, Set<string>>();
  for (const g of [a.graph ?? {}, b.graph ?? {}])
    for (const [q, n] of Object.entries(g))
      for (const r of n.refs) {
        if (!users.has(r)) users.set(r, new Set());
        users.get(r)!.add(q);
      }
  const info = (q: string) => b.graph?.[q] ?? a.graph?.[q];
  const queue = [...changed];
  while (queue.length) {
    const q = queue.pop()!;
    if (q.endsWith("()")) continue;
    const vias = q.startsWith("%") ? [q] : [q, `?${lastName(q)}`];
    if (appeared.has(q)) vias.push(`&${q}`);
    for (const via of vias)
      for (const u of users.get(via) ?? []) {
        if (changed.has(u)) continue;
        changed.add(u);
        queue.push(u);
        const n = info(u);
        if (n?.body) bodies.add(n.body);
        else n?.names.forEach((x) => names.add(x));
      }
  }
  return { all: null, names, bodies, changedKeys };
}

if (import.meta.main) {
  const [x, y] = process.argv.slice(2).map((p) => JSON.parse(readFileSync(p, "utf8")) as Idx);
  const r = semdiff(x, y);
  console.log(JSON.stringify({ all: r.all, names: [...r.names].sort(), bodies: [...r.bodies].sort(), changedKeys: r.changedKeys }, null, 1));
}
