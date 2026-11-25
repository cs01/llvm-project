// Throwaway prototype: how many lines of working C does a use-after-free rule
// over ClangIR reject, and how many known-bad programs does it catch?
// No annotations: frees, moves and returns are inferred inside one file.
//   bun uaf.ts LEVEL [-v] flat.cir...
// LEVEL says which borrows a free ends:
//   all    every pointer loaded from memory (no alias information)
//   type   loaded pointers to a type the free or call can free
//   root   those loaded through the same parameter, allocation or global
//          (assumes different roots never alias: not sound)
//   both   type and root
import { readFileSync } from "node:fs";
import { execSync } from "node:child_process";
const [level, ...rest] = process.argv.slice(2);
const verbose = rest[0] === "-v" && !!rest.shift();
const sdk = execSync("xcrun --show-sdk-path").toString().trim();
const lib = new Set(readFileSync(sdk + "/usr/lib/libSystem.B.tbd", "utf8").match(/[A-Za-z_$][\w$]*/g)!.map(s => s.replace(/^_/, "")));
const FREES: Record<string, number> = { free: 0, realloc: 0, reallocf: 0, fclose: 0, freeaddrinfo: 0, closedir: 0, pclose: 0, __builtin_free: 0 };
const ALLOC = new Set(["malloc", "calloc", "realloc", "reallocf", "strdup", "strndup", "aligned_alloc", "fopen"]);
type Fact = { o: string[]; loaded: boolean; stale: string; t: string };
type Op = { res: string[]; name: string; args: string[]; succ: string[]; attr: string; inT: string[]; outT: string; line: number };
type Block = { name: string; args: string[]; ops: Op[] };
type Fn = { name: string; params: string[]; blocks: Map<string, Block>; entry: string };
type Sum = { frees: Set<number>; types: Set<string>; stores: Set<number>; ret: Map<string, boolean>; roots: Set<string> };
const splitTop = (s: string) => { const r: string[] = []; let d = 0, c = ""; for (const ch of s) { if ("<([{".includes(ch)) d++; if (">)]}".includes(ch)) d--; if (ch === "," && !d) { r.push(c.trim()); c = ""; } else c += ch; } if (c.trim()) r.push(c.trim()); return r; };
function parse(text: string): Map<string, Fn> {
  const locs = new Map<string, string>();
  for (const m of text.matchAll(/^(#loc\d+) = loc\((.*)\)$/gm)) locs.set(m[1], m[2]);
  const line = (l: string, n = 0): number => { const m = l.match(/:(\d+):\d+/); if (m) return +m[1]; const r = l.match(/#loc\d+/); return r && n < 9 ? line(locs.get(r[0]) ?? "", n + 1) : 0; };
  const fns = new Map<string, Fn>(); let cur: Fn | null = null, b: Block | null = null, pend: Fn | null = null;
  for (const raw of text.split("\n")) {
    let m = raw.match(/^  "cir\.func"\(\).*sym_name = "([^"]+)"/);
    if (m) { pend = { name: m[1], params: [], blocks: new Map(), entry: "" }; cur = pend; b = null; continue; }
    if (!cur) continue;
    if (/^  \}\)/.test(raw)) { if (cur.entry) fns.set(cur.name, cur); cur = null; continue; }
    m = raw.match(/^  \^(bb\d+)(?:\((.*)\))?:/);
    if (m) { const args = [...(m[2] ?? "").matchAll(/(%\w+): /g)].map(x => x[1]); b = { name: m[1], args, ops: [] }; if (!cur.entry) { cur.entry = m[1]; cur.params = args; } cur.blocks.set(m[1], b); continue; }
    m = raw.match(/^    (?:(%\w+)(?::\d+)? = )?"(cir\.[\w.]+)"\(([^)]*)\)(?:\[([^\]]*)\])?(.*)$/);
    if (!m) { const n = raw.match(/^    (?:(%\w+)(?::\d+)? = )?"(cir\.[\w.]+)"/); if (n) throw Error("unparsed " + raw.slice(0, 120)); continue; }
    if (!b) { b = { name: "bb0", args: [], ops: [] }; cur.entry = "bb0"; cur.blocks.set("bb0", b); }
    const tail = m[5], ti = tail.lastIndexOf(" : ("), attr = tail.slice(0, ti < 0 ? undefined : ti);
    const tys = ti < 0 ? "" : tail.slice(ti + 3).replace(/ loc\(.*$/, ""); const ar = tys.lastIndexOf(" -> ");
    b.ops.push({ res: m[1] ? [m[1]] : [], name: m[2].slice(4), args: m[3] ? m[3].split(", ") : [], succ: (m[4] ?? "").match(/bb\d+/g) ?? [], attr, inT: splitTop(tys.slice(1, ar - 1)), outT: ar < 0 ? "" : tys.slice(ar + 4), line: line(tail.slice(tail.lastIndexOf(" loc(") )) });
  }
  return fns;
}
const isPtr = (t: string) => t.startsWith("!cir.ptr<");
const pointee = (t: string) => t.replace(/^!cir\.ptr<(.*)>$/, "$1");
const join = (a: Fact | undefined, b: Fact): Fact => !a ? b : { o: [...new Set([...a.o, ...b.o])].sort(), loaded: a.loaded || b.loaded, stale: a.stale || b.stale, t: a.t === b.t ? a.t : "!void" };
const same = (a: Fact, b: Fact) => a.o.join() === b.o.join() && a.loaded === b.loaded && a.stale === b.stale && a.t === b.t;
export function analyze(fns: Map<string, Fn>) {
  const sums = new Map<string, Sum>();
  for (const f of fns.keys()) sums.set(f, { frees: new Set(), types: new Set(), stores: new Set(), ret: new Map(), roots: new Set() });
  const size = () => [...sums.values()].reduce((n, s) => n + s.frees.size + s.types.size + s.stores.size + s.ret.size + s.roots.size, 0);
  let rep = new Map<string, string>(), before = -1;
  while (before !== size()) { before = size(); rep = new Map(); for (const f of fns.values()) run(f, fns, sums, rep); }
  return rep;
}
function run(f: Fn, fns: Map<string, Fn>, sums: Map<string, Sum>, rep: Map<string, string>) {
  type St = Map<string, Fact>; // "%v" values, "@place" local variables
  const inS = new Map<string, St>(), sum = sums.get(f.name)!;
  const entry: St = new Map(); f.params.forEach((p, i) => entry.set(p, { o: ["p" + i], loaded: false, stale: "", t: "" }));
  inS.set(f.entry, entry); const work = [f.entry];
  // addresses of local variables, flow-insensitive: value -> "@alloca.path"
  const place = new Map<string, string>(), fnOf = new Map<string, string>(), castFrom = new Map<string, string>(), valT = new Map<string, string>();
  for (const b of f.blocks.values()) for (const o of b.ops) { const r = o.res[0];
    if (r) valT.set(r, o.outT);
    if (o.name === "alloca") place.set(r, "@" + r);
    if (o.name === "get_global") fnOf.set(r, o.attr.match(/@"?([\w.$]+)/)?.[1] ?? "");
    if (o.name === "cast" && o.args[0]) castFrom.set(r, o.args[0]); }
  for (let ch = true; ch;) { ch = false; for (const b of f.blocks.values()) for (const o of b.ops) { const r = o.res[0], p = place.get(o.args[0]); if (!r || !p || place.has(r)) continue;
    if (o.name === "get_member") { place.set(r, p + "." + (o.attr.match(/name = "([^"]*)"/)?.[1] ?? "?")); ch = true; }
    else if (["cast", "ptr_stride", "get_element", "copy"].includes(o.name)) { place.set(r, p); ch = true; } } }
  const orgT = (v: string): string => { while (castFrom.has(v)) v = castFrom.get(v)!; return pointee(valT.get(v) ?? "!void"); };
  let n = 0;
  while (work.length && n++ < 20000) {
    const bn = work.pop()!, b = f.blocks.get(bn)!, s: St = new Map(inS.get(bn)!);
    const bad = (o: Op, why: string) => rep.set(f.name + ":" + o.line, why);
    const use = (v: string, o: Op, how: string) => { const x = s.get(v); if (x?.stale) bad(o, `${how} after ${x.stale}`); };
    const kill = (o: Op, facts: Fact | undefined, direct: boolean, types: Set<string> | null, what: string) => {
      const roots = facts?.o ?? [];
      const tyOk = (t: string) => !types || types.has(t) || types.has("!void") || t === "!void";
      for (const [k, x] of s) { if (x.stale) continue;
        const shared = x.o.some(y => roots.includes(y));
        const hit = direct && facts && !facts.loaded && shared ? true
          : x.loaded && (level === "all" || (level === "type" && tyOk(x.t)) || (level === "root" && shared) || (level === "both" && shared && tyOk(x.t)));
        if (hit) s.set(k, { ...x, stale: `${what} on line ${o.line}` }); } };
    const go = (t: string, args: string[]) => { const tb = f.blocks.get(t)!; const out: St = new Map(s);
      tb.args.forEach((a, i) => { const x = s.get(args[i]); if (x) out.set(a, x); });
      const old = inS.get(t); if (!old) { inS.set(t, out); work.push(t); return; }
      let ch = false; for (const [k, x] of out) { const c = old.get(k), j = join(c, x); if (!c || !same(c, j)) { old.set(k, j); ch = true; } } if (ch) work.push(t); };
    for (const o of b.ops) { const r = o.res[0], a0 = o.args[0];
      switch (o.name) {
      case "alloca": s.delete(r); break;
      case "get_global": s.set(r, { o: ["g:" + fnOf.get(r)], loaded: false, stale: "", t: pointee(o.outT) }); break;
      case "cast": case "ptr_stride": case "get_member": case "get_element": case "select": {
        for (const a of o.name === "select" ? o.args.slice(1) : [a0]) { const x = s.get(a); if (x) s.set(r, o.name === "select" ? join(s.get(r), x) : x); } break; }
      case "load": { const p = place.get(a0);
        if (p) { const x = s.get(p); if (x) s.set(r, x); else { s.delete(r); if (isPtr(o.outT)) s.set(r, { o: [p], loaded: true, stale: "", t: pointee(o.outT) }); } }
        else { use(a0, o, "read"); const x = s.get(a0); s.delete(r); if (isPtr(o.outT)) s.set(r, { o: x?.o ?? ["?"], loaded: true, stale: "", t: pointee(o.outT) }); } break; }
      case "store": { const [v, d] = o.args, p = place.get(d), x = s.get(v);
        if (p) { if (x) s.set(p, x); else s.delete(p); if (x && !isPtr(o.inT[0])) s.delete(p); }
        else { use(d, o, "write"); const base = s.get(d);
          if (x && isPtr(o.inT[0])) { if (!x.loaded) { for (const y of x.o) if (/^p\d/.test(y)) sum.stores.add(+y.slice(1));
            for (const [k, z] of s) if (!z.loaded && z.o.some(y => x.o.includes(y) && y[0] === "c")) s.set(k, { ...z, loaded: true, o: [...new Set([...z.o, ...(base?.o ?? [])])].sort(), t: orgT(v) }); } } } break; }
      case "copy": { use(a0, o, "write"); use(o.args[1], o, "read"); break; }
      case "call": case "try_call": {
        const cal = o.attr.match(/callee = @"?([\w.$]+)/)?.[1]; const ind = !cal; const args = ind ? o.args.slice(1) : o.args; const inT = ind ? o.inT.slice(1) : o.inT;
        const ptrs = args.map((a, i) => isPtr(inT[i] ?? "") ? a : "");
        for (const a of ptrs) if (a && !place.has(a)) use(a, o, "passed");
        const argF = (i: number) => ptrs[i] ? (place.has(ptrs[i]) ? { o: [place.get(ptrs[i])!], loaded: false, stale: "", t: "" } as Fact : s.get(ptrs[i])) : undefined;
        const note = (x: Fact | undefined, t: string) => { if (!x) return; for (const y of x.o) { if (!x.loaded && /^p\d/.test(y)) sum.frees.add(+y.slice(1)); else sum.roots.add(/^p\d/.test(y) ? y : y[0] === "g" ? y : "?"); } sum.types.add(t); };
        let ret: Fact | undefined = isPtr(o.outT) ? { o: ["c" + o.line + r], loaded: false, stale: "", t: pointee(o.outT) } : undefined;
        const callee = cal ? sums.get(cal) : undefined;
        if (cal && cal in FREES) { const x = argF(FREES[cal]); const t = orgT(args[FREES[cal]]); note(x, t); kill(o, x, true, new Set([t]), cal); }
        else if (callee) {
          const snap = args.map((_, i) => argF(i));
          const all: Fact = { o: [], loaded: true, stale: "", t: "" };
          for (const y of callee.roots) { if (y[0] === "g") all.o.push(y); else if (y === "?") snap.forEach(x => x && all.o.push(...x.o)); else all.o.push(...(snap[+y.slice(1)]?.o ?? [])); }
          for (const y of all.o) sum.roots.add(/^p\d/.test(y) || y[0] === "g" ? y : "?"); if (!all.o.length) sum.roots.delete("?");
          if (callee.roots.size || callee.frees.size) for (const t of callee.types) sum.types.add(t);
          if (callee.roots.size) kill(o, all, false, callee.types, cal + "()");
          for (const i of callee.frees) { note(snap[i], orgT(args[i] ?? "")); kill(o, snap[i], true, callee.types, cal + "()"); }
          for (const i of callee.stores) { const x = snap[i]; if (x && !x.loaded) { for (const y of x.o) if (/^p\d/.test(y)) sum.stores.add(+y.slice(1));
            for (const [k, z] of s) if (!z.loaded && z.o.some(y => x.o.includes(y) && y[0] === "c")) s.set(k, { ...z, loaded: true, o: [...new Set([...z.o, ...snap.flatMap((q, j) => j !== i && q ? q.o : [])])].sort(), t: orgT(args[i]) }); } }
          if (ret) { let j: Fact | undefined; for (const [y, ld] of callee.ret) { const src = /^p\d/.test(y) ? snap[+y.slice(1)] : undefined; const z: Fact = src ? { ...src, loaded: ld || src.loaded, t: pointee(o.outT), stale: "" } : y[0] === "g" ? { o: [y], loaded: ld, stale: "", t: pointee(o.outT) } : ret; j = join(j, z); } ret = j ?? ret; }
        } else if (ind) { // hooks: f(p) returning nothing frees; numbers in, pointer out allocates
          if (ptrs.length === 1 && ptrs[0] && !isPtr(o.outT) && (!process.env.PURE || true) && /\(\)|void/.test(o.outT)) { const x = argF(0), t = orgT(args[0]); note(x, t); kill(o, x, true, new Set([t]), "hook"); }
          else if (!process.env.PURE && ptrs.some(Boolean)) { const x: Fact = { o: ptrs.flatMap((_, i) => argF(i)?.o ?? []), loaded: true, stale: "", t: "" }; sum.types.add("!void"); sum.roots.add("?"); kill(o, x, false, null, "indirect call"); } }
        else if (cal && !fns.has(cal)) {
          if (!process.env.PURE && !lib.has(cal) && !/^(__builtin|llvm\.|__inline|__sput|__is|__to|__mem|__str|__snp|__spr)/.test(cal) && ptrs.some(Boolean)) { const x: Fact = { o: ptrs.flatMap((_, i) => argF(i)?.o ?? []), loaded: true, stale: "", t: "" }; sum.types.add("!void"); for (const y of x.o) sum.roots.add(/^p\d/.test(y) || y[0] === "g" ? y : "?"); kill(o, x, false, null, cal + "() (other file)"); }
          if (ret && !ALLOC.has(cal)) { const xs = ptrs.map((_, i) => argF(i)).filter(Boolean) as Fact[]; if (xs.length) ret = { ...xs.reduce((p, q) => join(p, q)), stale: "", t: pointee(o.outT) }; } }
        // an address handed out: whatever the callee left there is new
        for (const a of ptrs) { const p = a && place.get(a); if (p) for (const k of [...s.keys()]) if (k === p || k.startsWith(p + ".")) s.delete(k); }
        if (r) { if (ret) s.set(r, ret); else s.delete(r); } break; }
      case "return": { const x = a0 && s.get(a0); if (x && isPtr(valT.get(a0) ?? "")) { if (x.stale) bad(o, "returned after " + x.stale); for (const y of x.o) { const k = /^p\d/.test(y) || y[0] === "g" ? y : y[0] === "c" ? "fresh" : "?"; sum.ret.set(k, x.loaded || !!sum.ret.get(k)); } } break; }
      case "br": go(o.succ[0], o.args); break;
      case "brcond": { const seg = (o.attr.match(/operandSegmentSizes = array<i32: ([\d, ]+)>/)?.[1] ?? "1, 0, 0").split(", ").map(Number);
        go(o.succ[0], o.args.slice(1, 1 + seg[1])); go(o.succ[1], o.args.slice(1 + seg[1])); break; }
      default: if (o.succ.length) for (const t of o.succ) go(t, []); else if (r) { s.delete(r); } }
    }
  }
}
for (const file of rest) { const rep = analyze(parse(readFileSync(file, "utf8"))); console.log(`${file.split("/").pop()} ${level} ${rep.size}`); if (verbose) for (const [k, v] of rep) console.log("  " + k + " " + v); }
