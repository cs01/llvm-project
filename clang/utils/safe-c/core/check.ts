// Prototype checker for safe-c-notes/core-subset.md. Fails closed.
//   bun check.ts file.c [-v]    exit 0 accepted, 1 rejected, 2 could not judge
import { execFileSync, spawnSync } from "node:child_process";
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
const root = new URL("../../../..", import.meta.url).pathname, bin = (process.env.SC_BUILD ?? root + "build.noindex") + "/bin/";
const sdk = execFileSync("xcrun", ["--show-sdk-path"]).toString().trim();
const hdr = new URL("core.h", import.meta.url).pathname;
const CC1 = (file: string, extra: string[], act: string[]) => { const l = spawnSync(bin + "clang", ["-###", "-fsafe-c", "-w", "-include", hdr, "-isysroot", sdk, "-c", ...extra, file, "-o", "/dev/null"]).stderr.toString().trim().split("\n").pop()!; const a = [...l.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1].replace(/\\(.)/g, "$1")).slice(1); const i = a.indexOf("-emit-obj"); if (i < 0) throw new Reject("no cc1"); a.splice(i, 1); const o = a.lastIndexOf("-o"); a.splice(o, 2); const f = a.pop()!; return [...a, "-mllvm", "-safe-c-ownership=false", ...act, f]; };
class Reject extends Error {}
type Field = { owned: boolean; ptr: boolean; type: string };
type Rec = { fields: Map<string, Field>; owners: boolean };
type Param = { name: string; ptr: boolean; owned: boolean; konst: boolean; taken: "" | "true" | "zero"; pointee: string };
type Sig = { params: Param[]; retOwned: boolean; retPtr: boolean; hasBody: boolean; variadic: boolean };
const isPtrT = (t: string) => /\*/.test(t) || /\[\w*\]$/.test(t) && false;
const nStars = (t: string) => (t.match(/\*/g) ?? []).length, nOwned = (t: string) => (t.match(/\[\[clang::annotate_type/g) ?? []).length;
// what the outermost pointer points to: text before the last star
const pointeeOf = (t: string) => t.slice(0, t.lastIndexOf("*"));
const LIB: Record<string, "free" | "own" | "ownz" | "read" | "in0" | "stop"> = { free: "free", malloc: "own", calloc: "ownz", strdup: "own", printf: "read", puts: "read", putchar: "read", strlen: "read", strcmp: "read", strncmp: "read", memcmp: "read", abs: "read", atoi: "read", rand: "read", abort: "stop", exit: "stop", strchr: "in0" , snprintf: "read" };
function ast(file: string) {
  srcText = readFileSync(file, "latin1");
  const j = JSON.parse(execFileSync(bin + "clang", CC1(file, [], ["-ast-dump=json"]), { maxBuffer: 2 ** 30, stdio: ["ignore", "pipe", "ignore"] }).toString());
  const recs = new Map<string, Rec>(), sigs = new Map<string, Sig>(); const locals = new Map<string, boolean>(); const typedefs = new Map<string, string>();
  let f = ""; 
  const strip = (t: string) => t.replace(/\[\[clang::annotate_type\([^)]*\)\]\]|_Nullable|_Nonnull|__single|__bidi_indexable|__indexable|__null_terminated|__unsafe_indexable|__(counted|sized)_by(_or_null)?\([^)]*\)|__terminated_by\([^)]*\)/g, "").replace(/\s+/g, " ").trim();
  const userTop: any[] = []; const trusted = new Set<string>();
  // clang prints a file name only when it differs from the last one printed, in document order
  const visit = (n: any) => { if (!n || typeof n !== "object") return; if (Array.isArray(n)) { n.forEach(visit); return; } for (const k in n) { if (k === "includedFrom") continue; if (k === "file" && typeof n[k] === "string") f = n[k]; else visit(n[k]); } };
  for (const d of j.inner) { let mine = false; for (const k in d) { visit({ [k]: d[k] }); if (k === "loc") mine = Object.keys(d.loc).length > 0 && f === file && !d.isImplicit; } d.mine = mine; if (d.kind === "TypedefDecl") typedefs.set(d.name, d.type.desugaredQualType ?? d.type.qualType); if (d.mine) userTop.push(d); }
  const desugar = (t: any) => { let s: string = t.qualType; for (const w of s.match(/[A-Za-z_]\w*/g) ?? []) if (typedefs.has(w) && /\*|\[\[/.test(typedefs.get(w)!)) return (t.desugaredQualType as string) ?? s; return s; };
  const bare = (t: string) => { let s = strip(t).replace(/\b(const|volatile|struct|union|enum|restrict)\b/g, "").trim(); for (let i = 0; i < 9 && typedefs.has(s); i++) s = strip(typedefs.get(s)!).replace(/\b(const|volatile|struct|union|enum)\b/g, "").trim(); return s; };
  const addRec = (d: any) => { const name = d.name ?? "anon"; if (!d.inner || !d.completeDefinition) return; const r: Rec = { fields: new Map(), owners: false };
    for (const x of d.inner) { if (x.kind === "RecordDecl") addRec(x); if (x.kind !== "FieldDecl") continue; const t = desugar(x.type); const ptr = isPtrT(t); const full = (x.type.desugaredQualType ?? t) as string;
      if (ptr || isPtrT(full)) { if (/\(\*|\(\^/.test(full)) throw new Reject(`field ${x.name}: function pointers have no rule`); if (d.tagUsed === "union") throw new Reject(`union with a pointer`); if (/_Atomic/.test(full)) throw new Reject("atomic pointer"); if (nOwned(t) !== nStars(full.replace(/\[\[[^\]]*\]\]/g, "")) ) throw new Reject(`field '${x.name}' stores a pointer that is not an owner at every level`); r.owners = true; }
      r.fields.set(x.name ?? "", { owned: ptr, ptr, type: t }); const inner = recs.get(bare(full.replace(/\[.*/, ""))); if (inner?.owners && !ptr) { if (d.tagUsed === "union") throw new Reject("union holding owners"); r.owners = true; } }
    recs.set(name, r); };
  const hasOwners = (t: string) => { const b = bare(t.replace(/\[.*/, "")); return /\*/.test(b) || !!recs.get(b)?.owners; };
  for (const d of userTop) {
    if (d.kind === "RecordDecl") addRec(d);
    if (d.kind === "VarDecl") { const full = (d.type.desugaredQualType ?? d.type.qualType) as string; if (isPtrT(full) || hasOwners(full)) throw new Reject(`global '${d.name}' holds a pointer`); }
  }
  // _Trusted: the body is not checked, the signature is, and callers get the normal rules. Must be on the definition.
  for (const d of userTop) if (d.kind === "FunctionDecl" && (d.inner ?? []).some((x: any) => x.kind === "CompoundStmt") && (d.inner ?? []).some((x: any) => x.kind === "AnnotateAttr")) trusted.add(d.name);
  for (const d of j.inner) if (d.kind === "FunctionDecl") {
    const ps = (d.inner ?? []).filter((x: any) => x.kind === "ParmVarDecl"); const rt = (d.type.qualType as string); const ret = rt.slice(0, rt.search(/ ?\((?!\*)/) < 0 ? undefined : rt.indexOf(ps.length || /\(void\)$|\(\)$/.test(rt) ? "(" : "("));
    const trail = rt.match(/(\s*\[\[clang::annotate_type\(\.\.\.\)\]\])+$/)?.[0] ?? ""; const retHead = retType(rt.slice(0, rt.length - trail.length)) + trail, typedefRet = typedefs.get(bare(retHead)) ?? retHead;
    const s: Sig = { variadic: !!d.variadic, hasBody: (d.inner ?? []).some((x: any) => x.kind === "CompoundStmt"), retPtr: isPtrT(retHead) || isPtrT(typedefRet), retOwned: nOwned(retHead) > 0, params: ps.map((x: any) => { const t = desugar(x.type); const ptr = isPtrT(t) || isPtrT(x.type.desugaredQualType ?? ""); if ((x.inner ?? []).some((a: any) => a.kind === "AnnotateAttr")) throw new Reject("annotate"); const taken = nOwned(t) > 0 && /_Taken_if/.test(paramText(file, x)); const src = taken ? takenText(file, x) : ""; return { name: x.name ?? "", ptr, owned: nOwned(t) > 0, konst: /^const\b|\bconst *$/.test(pointeeOf(t).trim()), taken: src, pointee: pointeeOf(t) } as Param; }) };
    if (d.mine) { if (nStars(retHead) > 1) throw new Reject(`'${d.name}' returns a pointer to a pointer`); for (const p of s.params) { if (p.ptr && nStars(strip(p.pointee)) > 0) throw new Reject(`parameter '${p.name}' is a pointer to a pointer`); if (p.ptr && /\(\*/.test(p.pointee)) throw new Reject("function pointer parameter"); } for (const x of ps) if (!isPtrT(desugar(x.type)) && hasOwners(desugar(x.type))) throw new Reject("struct with owners passed by value"); if (!s.retPtr && hasOwners(retHead)) throw new Reject("struct with owners returned by value"); }
    const old = sigs.get(d.name); if (old && d.mine) { if (JSON.stringify([old.retOwned, old.params.map(p => [p.owned, p.konst, p.taken])]) !== JSON.stringify([s.retOwned, s.params.map(p => [p.owned, p.konst, p.taken])])) throw new Reject(`declarations of '${d.name}' disagree`); s.hasBody ||= old.hasBody; } sigs.set(d.name, s);
  }
  const scopes = new Map<string, string[]>(); let nextScope = 0; // "fn:local" -> scope paths of its declarations
  const walk = (n: any, fn: string, sc = "/") => { if (!n || typeof n !== "object") return; const k = n.kind as string;
    if (["GCCAsmStmt", "MSAsmStmt", "BlockExpr", "AtomicExpr", "CleanupAttr", "VAArgExpr", "CompoundLiteralExpr", "AddrLabelExpr", "IndirectGotoStmt", "StmtExpr"].includes(k) && k !== "StmtExpr") throw new Reject(`${k} has no rule`);
    if (k === "CStyleCastExpr" && written(n) && n.castKind !== "NullToPointer" && !nullCast(n) && isPtrT(n.type?.desugaredQualType ?? n.type?.qualType ?? "") ) throw new Reject("pointer cast");
    if (["CompoundStmt", "ForStmt", "IfStmt", "WhileStmt", "SwitchStmt", "DoStmt", "StmtExpr"].includes(k)) sc += ++nextScope + "/";
    if (k === "VarDecl") { scopes.set(fn + ":" + n.name, [...(scopes.get(fn + ":" + n.name) ?? []), sc]); const t = desugar(n.type), full = (n.type.desugaredQualType ?? t) as string; const p = isPtrT(full) && !/\]$/.test(full.replace(/\[\[[^\]]*\]\]/g, "").trim()); if (n.storageClass === "static" && (isPtrT(full) || hasOwners(full))) throw new Reject("static local holds a pointer"); if (/_Atomic/.test(full) && isPtrT(full)) throw new Reject("atomic pointer"); if (isPtrT(full) && /\(\*/.test(full)) throw new Reject("function pointer variable"); const arr = /\*[^*]*\[\w*\]$/.test(strip(full)); if ((arr || nStars(strip(full)) > 1) && nOwned(t) !== nStars(strip(full))) throw new Reject(`'${n.name}' holds pointers that are not owners`);
      const key = fn + ":" + n.name; const v = p ? (nOwned(t) > 0) : false; if (locals.has(key) && locals.get(key) !== v) throw new Reject(`two locals named '${n.name}' of different kinds`); locals.set(key, v); konst.set(key, (konst.get(key) ?? true) && /^const\b/.test(strip(t))); }
    for (const c of n.inner ?? []) walk(c, fn, sc); };
  const konst = new Map<string, boolean>();
  for (const d of userTop) { if (d.kind === "FunctionDecl") { if (!trusted.has(d.name)) walk(d, d.name); } else if (d.kind !== "RecordDecl" && d.kind !== "TypedefDecl" && d.kind !== "VarDecl" && d.kind !== "EnumDecl" && d.kind !== "EmptyDecl") throw new Reject(d.kind + " has no rule"); }
  return { recs, sigs, trusted, locals, scopes, konst, hasOwners, mine: new Set(userTop.filter(d => d.kind === "FunctionDecl").map(d => d.name as string)) };
}
// bounds checks contain casts the compiler made up; those do not start at a '('
let srcText = "";
const written = (n: any) => { const b = n.range?.begin ?? {}; return b.expansionLoc || b.spellingLoc || b.offset === undefined || srcText[b.offset] === "("; };
const nullCast = (n: any): boolean => { for (let c = n; c; c = c.inner?.[0]) if (c.kind === "IntegerLiteral") return c.value === "0"; return false; };
function retType(t: string) { // "R (args)" with nested parens
  let d = 0; for (let i = t.length - 1; i >= 0; i--) { if (t[i] === ")") d++; if (t[i] === "(") { d--; if (!d) return t.slice(0, i).trim(); } } return t; }
function paramText(file: string, x: any) { const off = (p: any) => p?.expansionLoc?.offset ?? p?.offset ?? 0; return readFileSync(file, "latin1").slice(off(x.range?.begin), off(x.range?.end) + 60).split(/[,;{]/)[0]; }
function takenText(file: string, x: any): "true" | "zero" { const l = paramText(file, x); if (/_Taken_if_zero/.test(l) && /_Taken_if_true/.test(l)) throw new Reject("cannot tell which taken mark"); return /_Taken_if_zero/.test(l) ? "zero" : "true"; }
type Op = { res: string; name: string; args: string[]; succ: string[]; attr: string; inT: string[]; outT: string; line: number };
type Blk = { args: string[]; ops: Op[] };
const splitTop = (s: string) => { const r: string[] = []; let d = 0, c = ""; for (let i = 0; i < s.length; i++) { const ch = s[i]; if (ch === "-" && s[i + 1] === ">") { c += "->"; i++; continue; } if ("<([{".includes(ch)) d++; if (">)]}".includes(ch)) d--; if (ch === "," && !d) { r.push(c.trim()); c = ""; } else c += ch; } if (c.trim()) r.push(c.trim()); return r; };
function parse(text: string, skip: Set<string>) {
  const locs = new Map<string, string>(); for (const m of text.matchAll(/^(#loc\d+) = loc\((.*)\)$/gm)) locs.set(m[1], m[2]);
  const line = (l: string, n = 0): number => { const m = l.match(/:(\d+):\d+/); if (m) return +m[1]; const r = l.match(/#loc\d+/); return r && n < 9 ? line(locs.get(r[0]) ?? "", n + 1) : 0; };
  const fns: { name: string; params: string[]; entry: string; blocks: Map<string, Blk> }[] = []; let cur: any = null, b: Blk | null = null;
  for (const raw of text.split("\n")) { let m = raw.match(/^  "cir\.func"\(\).*sym_name = "([^"]+)"/);
    if (m) { cur = skip.has(m[1]) ? null : { name: m[1], params: [], entry: "", blocks: new Map() }; b = null; continue; } if (!cur) continue;
    if (/^  \}\)/.test(raw)) { if (cur.entry) fns.push(cur); cur = null; continue; } if (!raw.trim()) continue;
    m = raw.match(/^  \^(bb\d+)(?:\((.*)\))?:/); if (m) { const args = [...(m[2] ?? "").matchAll(/(%\w+): /g)].map(x => x[1]); b = { args, ops: [] }; if (!cur.entry) { cur.entry = m[1]; cur.params = args; } cur.blocks.set(m[1], b); continue; }
    m = raw.match(/^    (?:(%\w+)(?::\d+)? = )?"(\w+\.[\w.]+)"\(([^)]*)\)(?:\[([^\]]*)\])?(.*)$/); if (!m || !m[2].startsWith("cir.")) throw new Reject("unparsed IR: " + raw.trim().slice(0, 80));
    if (!b) { b = { args: [], ops: [] }; cur.entry = "bb0"; cur.blocks.set("bb0", b); }
    const tail = m[5], ti = tail.lastIndexOf(" : ("), attr = tail.slice(0, ti < 0 ? undefined : ti), tys = ti < 0 ? "" : tail.slice(ti + 3).replace(/ loc\(.*$/, ""), ar = tys.lastIndexOf(") -> ");
    b.ops.push({ res: m[1] ?? "", name: m[2].slice(4), args: m[3] ? m[3].split(", ") : [], succ: (m[4] ?? "").match(/bb\d+/g) ?? [], attr, inT: splitTop(tys.slice(1, ar)), outT: ar < 0 ? "" : tys.slice(ar + 5), line: line(tail.slice(tail.lastIndexOf(" loc("))) }); }
  return fns;
}
// ---- abstract state
type Node = { k: "own" | "dead" | "static" | "maybe" | "borrow"; p?: string; cap?: boolean; tok?: string; sense?: boolean; why?: string };
type Fact = { tok: string; sense: boolean } | { c: number }; // scalar: equals a call's result (sense: nonzero), or a constant
type St = { n: Map<string, Node>; f: Map<string, Fact> };
const clone = (s: St): St => ({ n: new Map(s.n), f: new Map(s.f) });
const ptrT = (t: string) => t.startsWith("!cir.ptr<"), cirPointee = (t: string) => t.slice(9, -1).replace(/, target_address_space.*$/, "");
export function check(file: string): string[] {
  const A = ast(file); const out: string[] = []; const dir = mkdtempSync(tmpdir() + "/core");
  try {
    const r1 = spawnSync(bin + "clang", CC1(file, ["-fclangir"], ["-emit-cir", "-o", dir + "/a.cir"])); if (r1.status !== 0) throw new Reject("no ClangIR: " + r1.stderr.toString().split("\n")[0].slice(0, 100));
    const r2 = spawnSync(bin + "cir-opt", [dir + "/a.cir", "--cir-flatten-cfg", "--cir-goto-solver", "--mlir-print-op-generic", "--mlir-print-debuginfo", "-o", dir + "/a.flat"]); if (r2.status !== 0) throw new Reject("no flat ClangIR");
    const text = readFileSync(dir + "/a.flat", "utf8"); const seen = new Set<string>();
    for (const f of parse(text, A.trusted)) { seen.add(f.name); if (A.trusted.has(f.name)) continue; try { fn(f, A); } catch (e) { if (e instanceof Reject) out.push(f.name + ": " + e.message); else throw e; } }
    for (const m of A.mine) if (A.sigs.get(m)!.hasBody && !seen.has(m) ) { /* unused static inline functions are not emitted */ }
  } finally { rmSync(dir, { recursive: true }); }
  return out;
}
type Val = { t: "node"; n: string } | { t: "addr"; n: string } | { t: "local"; a: string } | { t: "null" } | { t: "owned"; lib?: boolean; zero?: boolean; typed?: boolean } | { t: "ownref"; a: string; n: string } | { t: "static" } | { t: "fn" };
function fn(f: ReturnType<typeof parse>[0], A: ReturnType<typeof ast>) {
  const sig = A.sigs.get(f.name); if (!sig) throw new Reject("no declaration"); const V = new Map<string, Val>(); const ownedLocal = new Map<string, boolean>(), ptrLocal = new Map<string, string>(), capLocal = new Map<string, boolean>();
  const firstBorrow = sig.params.findIndex(p => p.ptr && !p.owned); let opLine = 0; const no = (m: string): never => { throw new Reject(`line ${opLine}: ${m}`); };
  const entry: St = { n: new Map(), f: new Map() };
  f.params.forEach((v, i) => { const p = sig.params[i]; if (!p) throw new Reject("extra parameter"); if (!p.ptr) return; if (p.owned) entry.n.set(v, { k: "own" }); else entry.n.set(v, { k: "borrow", cap: !p.konst }); });
  const ownersIn = (t: string): boolean => { if (ptrT(t)) return true; const m = t.match(/^!rec_(\w+)/); if (m) { const r = A.recs.get(m[1]); return r ? r.owners : !/^(__sFILE)/.test(m[1]); } const a = t.match(/^!cir\.array<(.*) x \d+>$/); if (a) return ownersIn(a[1]); return false; };
  const inS = new Map<string, St>([[f.entry, entry]]); const work = [f.entry]; let steps = 0;
  const useBlock = (bn: string, s: St) => {
    const get = (x: string) => s.n.get(x); const isAnc = (a: string, of: string): boolean => { for (let c: string | undefined = of, i = 0; c && i < 500; i++) { if (c === a) return true; c = s.n.get(c)?.p; } return false; };
    const rootOf = (x: string) => { let c = x; for (let i = 0; i < 500; i++) { const n = s.n.get(c); if (!n?.p) return c; c = n.p; } return no("parent chain"); };
    const live = (x: string, what: string) => { const n = get(x); if (!n) return no(`${what}: '${x}' may be unset`); if (n.k === "dead") return no(`${what} after ${n.why ?? "it died"}`); if (n.k === "maybe") return no(`${what} before testing whether the call took it`); return n; };
    const kill = (x: string, why: string) => { const n = get(x); if (n && n.k !== "static") s.n.set(x, { k: "dead", why }); };
    const killDesc = (x: string, why: string) => { for (const k of [...s.n.keys()]) if (k !== x && get(k)!.k === "borrow" && isAnc(x, k)) victims.push(k); for (const k of victims.splice(0)) kill(k, why); }; const victims: string[] = [];
    const mutate = (x: string, why: string) => { const n = live(x, "change"); if (n.k === "static") no("change through a static pointer"); if (n.k === "borrow" && !n.cap) no("this pointer is const, so nothing it owns can be changed"); const r = rootOf(x); const anc = new Set<string>(); for (let c: string | undefined = x; c; c = get(c)?.p) anc.add(c); for (const k of s.n.keys()) if (get(k)!.k === "borrow" && !anc.has(k) && rootOf(k) === r) victims.push(k); for (const k of victims.splice(0)) kill(k, why); };
    const reparent = (x: string) => { const p = get(x)?.p; for (const [k, n] of s.n) if (n.p === x) s.n.set(k, p ? { ...n, p } : { k: "dead", why: "its source was reassigned" }); };
    const moveOwner = (a: string, why: string) => { const n = live(a, why); if (n.k !== "own") no("not an owner"); killDesc(a, why + " on line " + opLine); s.n.set(a, { k: "dead", why: why + " on line " + opLine }); for (const [k, m] of s.n) if (m.k === "maybe" && k === a) s.n.delete(k); };
    const setSSA = (r: string, n: Node, v: Val) => { reparent(r); s.n.set(r, n); V.set(r, v); };
    // base object behind a pointer value, as a live node
    const nodeOf = (x: string, what: string): string | null => { const v = V.get(x); if (!v) return no(`${what}: value with no rule`); switch (v.t) { case "node": case "addr": live(v.n, what); return v.n; case "ownref": live(v.n, what); return v.n; case "local": return v.a; case "static": case "fn": case "null": return null; case "owned": { const n = get(x); if (!n) return no(what + ": owner used after it moved"); live(x, what); return x; } } };
    const takeOwned = (x: string, why: string) => { const v = V.get(x)!; if (!v) no("value with no rule"); if (v.t === "null") return; if (v.t === "owned") { if (get(x)?.k !== "own") no(why + ": already moved"); moveOwner(x, why); return; } if (v.t === "ownref") { live(v.n, why); moveOwner(v.a, why); return; } no(why + ": this is a borrow, not an owner (use _Take to move out of a field)"); };
    const b = f.blocks.get(bn)!; const go = (t: string, args: string[], st: St) => { const tb = f.blocks.get(t)!; const o = clone(st); tb.args.forEach((a, i) => { const v = V.get(args[i]); const fa = st.f.get(args[i]); if (fa) o.f.set(a, fa); else o.f.delete(a); if (!v) return; const prev = V.get(a); let nv: Val = v, nn: Node | undefined; if (v.t === "ownref") { nv = { t: "node", n: a }; nn = { k: "borrow", p: v.n, cap: true }; } else if (v.t === "node" || v.t === "addr") { nv = { t: v.t, n: a }; nn = { k: "borrow", p: v.n, cap: st.n.get(v.n)?.cap ?? st.n.get(v.n)?.k === "own" }; if (st.n.get(v.n)?.k === "static") nn = { k: "static" }; if (st.n.get(v.n)?.k === "dead") nn = { k: "dead" }; } else if (v.t === "null" || v.t === "static") { nv = prev ?? v; nn = { k: "static" }; if (!prev) nv = { t: "node", n: a }; } else if (v.t === "owned") { no("an owner flows through '?:', which has no rule"); } else if (v.t === "local") no("address flows through '?:'");
      if (prev && prev.t !== nv.t) { if (prev.t === "node" && nv.t === "addr" || prev.t === "addr" && nv.t === "node") nv = { t: "addr", n: a }; else no("mixed kinds flow together"); } V.set(a, nv); if (nn) { for (const [k, n] of o.n) if (n.p === a) o.n.set(k, { k: "dead", why: "loop" }); o.n.set(a, nn); } });
      const old = inS.get(t); if (!old) { inS.set(t, o); work.push(t); return; } let ch = false;
      const was = clone(old); const anc = (S: St, x: string) => { const r: string[] = []; for (let c: string | undefined = S.n.get(x)?.p, i = 0; c && i < 500; i++) { r.push(c); c = S.n.get(c)?.p; } return r; };
      for (const [k, x] of [...old.n]) { const y = o.n.get(k); let j: Node | undefined; if (!y) j = undefined; else if (x.k === "dead" || y.k === "dead") j = { k: "dead", why: x.why ?? y.why }; else if (x.k === y.k && x.p === y.p && x.tok === y.tok && x.sense === y.sense) j = x.k === "borrow" ? { ...x, cap: x.cap && y.cap } : x; else if (x.k === "static" && y.k === "borrow") j = y; else if (y.k === "static" && x.k === "borrow") j = x; else if (x.k === "borrow" && y.k === "borrow") { const ya = anc(o, k); const c = anc(was, k).find(q => ya.includes(q)); j = c ? { k: "borrow", p: c, cap: x.cap && y.cap } : { k: "dead", why: "it points to different things on different paths" }; } else j = { k: "dead", why: "it was moved on one path" };
        if (!j) { old.n.delete(k); ch = true; } else if (JSON.stringify(j) !== JSON.stringify(x)) { old.n.set(k, j); ch = true; } }
      for (const [k, n] of old.n) if (n.p && !old.n.has(n.p)) { const from = was.n.get(k)?.p === n.p ? was : o.n.get(k)?.p === n.p ? o : undefined; const up = from && anc(from, k).find(q => old.n.has(q) && q !== k); if (up && n.k === 'borrow') { old.n.set(k, { ...n, p: up }); ch = true; continue; } old.n.set(k, { k: "dead", why: "its source is gone" }); ch = true; }
      for (const [k, n] of old.n) if (n.p && old.n.get(n.p)!.k === "dead" && n.k === "borrow") { old.n.set(k, { k: "dead", why: old.n.get(n.p)!.why }); ch = true; }
      for (const [k, x] of [...old.f]) if (JSON.stringify(o.f.get(k)) !== JSON.stringify(x)) { old.f.delete(k); ch = true; }
      if (ch) work.push(t); };
    for (let oi = 0; oi < b.ops.length; oi++) { const o = b.ops[oi]; if (o.line) opLine = o.line; const r = o.res, a0 = o.args[0];
      switch (o.name) {
      case "alloca": { const pt = cirPointee(o.outT), name = o.attr.match(/name = "([^"]*)"/)?.[1] ?? ""; reparent(r); const isP = ptrT(pt); if (/count|dynAlloc/.test(o.attr) || o.args.length) { if (ownersIn(pt)) no("variable-length array of owners"); }
        for (const [k, n] of s.n) if (n.p === r) s.n.set(k, { k: "dead", why: "its variable's scope ended" });
        if (isP) { let own = false, cap = true; const pi = sig.params.findIndex(p => p.name === name && p.ptr); const lk = f.name + ":" + name; if (name === "__retval") own = sig.retOwned; else if (A.locals.has(lk) && pi >= 0) return no(`local '${name}' has the name of a parameter`); else if (A.locals.has(lk)) { own = A.locals.get(lk)!; cap = !A.konst.get(lk); } else if (pi >= 0) { own = sig.params[pi].owned; } else return no(`temporary '${name}' holds a pointer`); ownedLocal.set(r, own); ptrLocal.set(r, name); capLocal.set(r, cap); s.n.delete(r); } else { s.n.set(r, { k: "own" }); varName.set(r, name); }
        V.set(r, { t: "local", a: r }); s.f.delete(r); break; }
      case "const": { if (ptrT(o.outT)) { if (!/#cir\.ptr<null>/.test(o.attr)) no("constant pointer with no rule"); V.set(r, { t: "null" }); } else { if (/^!rec_|^!cir\.array/.test(o.outT) && ownersIn(o.outT) && !/#cir\.zero/.test(o.attr)) no("constant aggregate of pointers"); const c = o.attr.match(/#cir\.int<(-?\d+)>/) ; if (c) s.f.set(r, { c: +c[1] }); else if (/true>|bool<true>/.test(o.attr)) s.f.set(r, { c: 1 }); else if (/bool<false>|false>/.test(o.attr)) s.f.set(r, { c: 0 }); } break; }
      case "get_global": { const nm = o.attr.match(/@"?([\w.$]+)/)?.[1] ?? ""; V.set(r, A.sigs.has(nm) ? { t: "fn" } : { t: "static" }); if (!A.sigs.has(nm) && ownersIn(cirPointee(o.outT)) && !/^\.str/.test(nm) && !/^__std(err|out|in)p$/.test(nm)) no("global holding pointers"); break; }
      case "cast": { const k = o.attr.match(/cir\.cast<(\w+)>/)?.[1] ?? ""; if (!ptrT(o.inT[0]) && !ptrT(o.outT)) { const x = s.f.get(a0); const bits = (t: string) => t === "!cir.bool" ? 1 : +(t.match(/^![su](\d+)i$/)?.[1] ?? t.match(/^!cir\.int<[su], (\d+)>$/)?.[1] ?? -1); const bi = bits(o.inT[0]), bo = bits(o.outT);
          if (x && (k === "int_to_bool" || k === "bool_to_int" || (k === "integral" && bi > 0 && bo >= bi))) s.f.set(r, "c" in x && k === "int_to_bool" ? { c: x.c ? 1 : 0 } : x); else s.f.delete(r); break; }
        if (k === "ptr_to_bool" || (k === "ptr_to_int")) { if (k === "ptr_to_int") no("pointer to integer"); nodeOf(a0, "test"); s.f.delete(r); break; }
        if (k === "array_to_ptrdecay") { const v = V.get(a0)!; if (v.t === "local") { reparent(r); s.n.set(r, { k: "borrow", p: v.a, cap: true }); V.set(r, { t: "addr", n: r }); } else V.set(r, v); break; }
        if (k === "bitcast") { const v = V.get(a0); if (!v) return no("cast of a value with no rule"); const F = cirPointee(o.inT[0]), T = cirPointee(o.outT); if (v.t === "null" || v.t === "fn") { V.set(r, v); break; } const vd = (t: string) => t === "!void" || t === "!cir.void";
          if (v.t === "owned" && vd(F)) { if (ownersIn(T) && !v.zero) no("memory for owners must come from calloc"); if (v.typed) no("second type for one allocation"); v.typed = true; s.n.set(r, s.n.get(a0) ?? { k: "dead" }); s.n.delete(a0); V.set(r, { t: "owned", typed: true, zero: v.zero }); break; }
          if (vd(T) && b.ops.slice(oi + 1).every(q => !q.args.includes(r) || (q.name === "call" && /callee = @free[,\s>]/.test(q.attr)))) { if (v.t === "owned") { s.n.set(r, s.n.get(a0)!); s.n.delete(a0); } V.set(r, v.t === "owned" ? { t: "owned" } : v); break; }
          if (F === T) { if (v.t === "owned") { s.n.set(r, s.n.get(a0) ?? { k: "dead" }); s.n.delete(a0); V.set(r, { ...v }); } else V.set(r, v); break; }
          if (!ownersIn(F) && !ownersIn(T) && !/rec_/.test(F + T) && v.t !== "owned") { if (v.t === "local") { if (ptrLocal.has(v.a)) no("cast of a pointer variable's address"); reparent(r); s.n.set(r, { k: "borrow", p: v.a, cap: true }); V.set(r, { t: "addr", n: r }); } else V.set(r, v); break; } no("pointer cast between different types"); }
        no(`cast ${k}`); break; }
      case "get_member": case "ptr_stride": case "get_element": { const v = V.get(a0); if (!v) return no("value with no rule"); if (v.t === "local") { if (ptrLocal.has(v.a) ) no("address of a pointer variable"); reparent(r); s.n.set(r, { k: "borrow", p: v.a, cap: true }); V.set(r, { t: "addr", n: r }); break; } if (v.t === "static") { V.set(r, v); break; } const n = nodeOf(a0, "use"); if (n === null) { V.set(r, { t: "static" }); break; } const nn = get(n)!; if (nn.k === "static") { V.set(r, { t: "static" }); break; } reparent(r); s.n.set(r, { k: "borrow", p: n, cap: nn.k === "own" ? true : nn.cap }); V.set(r, { t: "addr", n: r }); break; }
      case "load": { const v = V.get(a0); if (!v) return no("load from a value with no rule"); if (v.t === "local") { if (!ptrLocal.has(v.a)) { if (ownersIn(o.outT)) no("copy of a struct with owners"); const x = s.f.get(v.a); if (x) s.f.set(r, x); else s.f.delete(r); break; }
          const own = ownedLocal.get(v.a)!; const n = get(v.a); reparent(r); if (!n) return no(`'${ptrLocal.get(v.a)}' may be unset`); if (n.k === "static") { s.n.set(r, { k: "static" }); V.set(r, { t: "node", n: r }); break; } if (n.k === "dead" || n.k === "maybe") { s.n.set(r, n.k === "dead" ? n : { k: "dead", why: "a call may have taken it; test its result first" }); V.set(r, own ? { t: "ownref", a: v.a, n: r } : { t: "node", n: r }); break; }
          s.n.set(r, { k: "borrow", p: v.a, cap: own ? true : n.cap }); V.set(r, own ? { t: "ownref", a: v.a, n: r } : { t: "node", n: r }); break; }
        const n = nodeOf(a0, "read"); if (!ptrT(o.outT)) { if (ownersIn(o.outT)) no("copy of a struct with owners"); s.f.delete(r); break; } if (n === null || get(n)!.k === "static") { no("pointer loaded from static data"); break; }
        // load then null of the same slot: a move out of the place
        let take = false; for (const q of b.ops.slice(oi + 1)) { if (q.name === "store" && q.args[1] === a0) { take = V.get(q.args[0])?.t === "null" || b.ops.some(z => z.res === q.args[0] && z.name === "const" && /#cir\.ptr<null>/.test(z.attr)); break; } if (q.args.includes(r) || !["const"].includes(q.name)) break; }
        if (take) { mutate(n!, `_Take on line ${opLine}`); reparent(r); s.n.set(r, { k: "own" }); V.set(r, { t: "owned", typed: true }); break; }
        const nn = get(n!)!; reparent(r); s.n.set(r, { k: "borrow", p: n!, cap: nn.k === "own" ? true : nn.cap }); V.set(r, { t: "node", n: r }); break; }
      case "store": { const [v, d] = o.args; const dv = V.get(d); if (!dv) return no("store to a value with no rule"); const pT = ptrT(o.inT[0]);
        if (dv.t === "local") { if (!ptrLocal.has(dv.a)) { if (!pT && ownersIn(o.inT[0])) { const c = b.ops.find(q => q.res === v); if (!(c && c.name === "const" && /#cir\.zero/.test(c.attr))) no("copy of a struct with owners"); killDesc(dv.a, "overwritten"); } const x = s.f.get(v); if (x && !escapes.has(dv.a)) s.f.set(dv.a, x); else s.f.delete(dv.a); break; }
          const a = dv.a; if (f.params.includes(v)) { const n = get(v); if (!n) return no("parameter"); if (ownedLocal.get(a)) { s.n.set(a, { k: "own" }); s.n.delete(v); } else s.n.set(a, { k: "borrow", p: v, cap: n.cap }); V.set(v, { t: "node", n: v }); break; }
          const x = V.get(v); if (!x) return no("pointer value with no rule"); if (ownedLocal.get(a)) { takeOwned(v, "moved"); killDesc(a, "its owner was reassigned"); s.n.set(a, { k: "own" }); break; }
          if (x.t === "null" || x.t === "static" ) { reparent(a); s.n.set(a, { k: "static" }); break; } if (x.t === "fn") no("function pointer"); if (x.t === "owned") no("the result is an owner; store it in an '_Owned' variable");
          const n = nodeOf(v, "copy"); if (n === null) { reparent(a); s.n.set(a, { k: "static" }); break; } const nn = get(n)!; if (nn.k === "static") { reparent(a); s.n.set(a, { k: "static" }); break; }
          const src = x.t === "ownref" ? x.a : n; const cap = (nn.k === "own" ? true : !!nn.cap) && capLocal.get(a)!; reparent(a); s.n.delete(a); let p: string | undefined = src; while (p && (p === a || (p[0] === "%" && !ptrLocal.has(p) && get(p)?.k === "borrow" && get(p)?.p))) p = get(p)?.p ?? (p === a ? undefined : p); if (!p) return no("borrow of itself");
          // a variable may not be pointed to from outside its block
          const rv = varName.get(rootOf(p)); if (rv !== undefined) { const inner = A.scopes.get(f.name + ":" + rv), outer = A.scopes.get(f.name + ":" + ptrLocal.get(a)); if (!inner || !outer) no(`'${ptrLocal.get(a)}' points to a variable or temporary with no known block`); else if (inner.some(x => outer.some(y => !y.startsWith(x)))) no(`'${ptrLocal.get(a)}' outlives '${rv}', which it would point to`); } s.n.set(a, { k: "borrow", p, cap }); break; }
        if (!pT) { const n = nodeOf(d, "write"); if (n === null || get(n)!.k === "static") { if (ownersIn(o.inT[0])) no("copy of a struct with owners"); break; } if (ownersIn(o.inT[0])) no("copy of a struct with owners"); break; }
        const prevTake = b.ops.slice(0, oi).some(q => q.name === "load" && q.args[0] === d && V.get(q.res)?.t === "owned"); if (prevTake && V.get(v)?.t === "null") { break; }
        takeOwned(v, "stored"); const n = nodeOf(d, "write"); if (n === null) return no("owner stored in static data"); mutate(n, `a store on line ${opLine}`); break; }
      case "copy": case "libc.memcpy": case "libc.memset": case "libc.memmove": { for (let i = 0; i < Math.min(2, o.args.length); i++) if (ptrT(o.inT[i] ?? "")) { if (ownersIn(cirPointee(o.inT[i]))) no(o.name + " of owners"); const v = V.get(o.args[i]); if (v?.t === "local" && ptrLocal.has(v.a)) no("address of a pointer variable"); nodeOf(o.args[i], o.name); } if (r) V.set(r, V.get(a0)!); break; }
      case "call": { const cal = o.attr.match(/callee = @"?([\w.$]+)/)?.[1]; if (!cal) return no("indirect call"); for (const [k, n] of s.n) if (n.k === "maybe" && n.tok === r && r) s.n.set(k, { k: "dead", why: "tested too late" }); for (const [k, x] of s.f) if ("tok" in x && x.tok === r) s.f.delete(k);
        const sg = A.sigs.get(cal); const resOwn = (z: boolean, typed: boolean) => { reparent(r); s.n.set(r, { k: "own" }); V.set(r, { t: "owned", zero: z, typed }); };
        for (let i = 0; i < o.args.length; i++) if (ptrT(o.inT[i] ?? "")) { const v = V.get(o.args[i]); if (!v) return no("argument with no rule"); if (v.t === "local" && ptrLocal.has(v.a)) no("address of a pointer variable passed"); if (v.t === "fn") no("function passed as an argument"); }
        if (!A.mine.has(cal)) { const k = LIB[cal.replace(/^__builtin_/, "")]; if (!k) return no(`'${cal}' has no rule`); if (k === "free") { takeOwned(a0, "freed"); break; } for (let i = 0; i < o.args.length; i++) if (ptrT(o.inT[i] ?? "") ) { nodeOf(o.args[i], "passed"); if (ownersIn(cirPointee(o.inT[i]))) no(`owners passed to '${cal}'`); if (V.get(o.args[i])!.t === "owned") no("owner would be lost"); }
          if (k === "own") resOwn(false, cal === "strdup"); else if (k === "ownz") resOwn(true, false); else if (k === "in0") { const n = nodeOf(a0, "passed"); reparent(r); if (n === null) V.set(r, { t: "static" }); else { s.n.set(r, { k: "borrow", p: n, cap: false }); V.set(r, { t: "node", n: r }); } } else if (r && ptrT(o.outT)) no("result with no rule"); else if (r) s.f.delete(r); break; }
        const P = sg!.params; if (o.args.length > P.length) no("extra arguments"); const uses: { n: string; mut: boolean; i: number }[] = [];
        for (let i = 0; i < o.args.length; i++) { const p = P[i]; if (!p.ptr) continue; if (p.owned) continue; const n = nodeOf(o.args[i], `passed to '${cal}'`); if (n === null || get(n)!.k === "static") { if (!p.konst && ownersIn(cirPointee(o.inT[i])) && V.get(o.args[i])!.t !== "null" && get(n ?? "")?.k !== "static") no("static data passed as changeable"); continue; } uses.push({ n, mut: !p.konst && ownersIn(cirPointee(o.inT[i])), i }); }
        for (const u of uses) for (const w of uses) if (u !== w && u.mut && rootOf(u.n) === rootOf(w.n)) no(`two arguments of '${cal}' reach the same owner and one may be changed`);
        const resParent = firstB(sg!) >= 0 ? uses.find(u => u.i === firstB(sg!)) : undefined; const resCap = resParent ? (get(resParent.n)!.k === "own" ? true : !!get(resParent.n)!.cap) : false;
        // owned arguments leave first: a borrow of one of them passed beside it is dead
        for (let i = 0; i < o.args.length; i++) { const p = P[i]; if (!p.ptr || !p.owned) continue; const v = V.get(o.args[i])!; if (p.taken) { if (v.t === "null") continue; if (v.t !== "ownref") return no("a conditionally taken owner must be a variable"); live(v.n, "passed"); const a = v.a; killDesc(a, `passed to '${cal}'`); if (get(a)!.k !== "own") no("not an owner"); pending.push([a, p.taken]); s.n.set(a, { k: "dead", why: "passed twice" }); } else takeOwned(o.args[i], `passed to '${cal}'`); }
        for (const u of uses) { live(u.n, `passed to '${cal}'`); } for (const u of uses) if (u.mut) mutate(u.n, `the call to '${cal}' on line ${opLine}`);
        for (const [a, how] of pending.splice(0)) s.n.set(a, { k: "maybe", tok: r, sense: how === "true" });
        if (r && !ptrT(o.outT)) { s.f.set(r, { tok: r, sense: true }); if (/rec_/.test(o.outT) && ownersIn(o.outT)) no("struct with owners returned"); }
        else if (r) { if (sg!.retOwned) resOwn(true, true); else if (!resParent) { if (firstB(sg!) < 0) no(`'${cal}' returns a borrow of nothing`); reparent(r); s.n.set(r, { k: "static" }); V.set(r, { t: "node", n: r }); } else { live(resParent.n, "result"); reparent(r); s.n.set(r, { k: "borrow", p: resParent.n, cap: resCap }); V.set(r, { t: "node", n: r }); } } break; }
      case "return": { if (a0 && ptrT(o.inT[0])) { const v = V.get(a0); if (!v) return no("returned value with no rule"); if (sig.retOwned) { takeOwned(a0, "returned"); } else { if (v.t === "null" || v.t === "static") { if (v.t === "static" && sig.retPtr && false) no(""); } else { const n = nodeOf(a0, "returned"); if (n !== null && get(n)!.k !== "static") { const rt = rootOf(n); if (firstBorrow < 0 || rt !== f.params[firstBorrow]) no("may only return what is reached through the first borrowed parameter"); } } } }
        for (let i = 0; i < sig.params.length; i++) { const p = sig.params[i]; if (!p.taken) continue; const al = [...ptrLocal].find(([, n]) => n === p.name)?.[0]; const n = al ? get(al) : undefined; const x = a0 ? s.f.get(a0) : undefined; if (!x || !("c" in x)) return no("returns a value that does not say whether the owner was taken"); const success = p.taken === "true" ? x.c !== 0 : x.c === 0; if (success ? n?.k === "own" : n?.k !== "own") no(success ? `success without taking '${p.name}'` : `failure after taking '${p.name}'`); if (reassigned.has(p.name)) no(`'${p.name}' is reassigned`); } break; }
      case "br": go(o.succ[0], o.args, s); return;
      case "brcond": { const seg = (o.attr.match(/operandSegmentSizes = array<i32: ([\d, ]+)>/)?.[1] ?? "1, 0, 0").split(", ").map(Number); const x = s.f.get(a0); for (const side of [0, 1]) { const st = clone(s); if (x && "tok" in x) for (const [k, n] of st.n) if (n.k === "maybe" && n.tok === x.tok) { const nonzero = (side === 0) === x.sense; st.n.set(k, nonzero === n.sense ? { k: "dead", why: "the call took it" } : { k: "own" }); } go(o.succ[side], side ? o.args.slice(1 + seg[1]) : o.args.slice(1, 1 + seg[1]), st); } return; }
      case "switch.flat": { for (const t of o.succ) go(t, [], s); if (o.args.length > 1) no("switch with operands"); return; }
      case "cmp": { if (ptrT(o.inT[0])) { nodeOf(a0, "compared"); nodeOf(o.args[1], "compared"); s.f.delete(r); break; } const x = s.f.get(a0), y = s.f.get(o.args[1]); const eq = /<eq>/.test(o.attr), ne = /<ne>/.test(o.attr); if (x && "tok" in x && y && "c" in y && y.c === 0 && (eq || ne)) s.f.set(r, { tok: x.tok, sense: eq ? !x.sense : x.sense }); else s.f.delete(r); break; }
      case "not": { const x = s.f.get(a0); if (x && "tok" in x) s.f.set(r, { tok: x.tok, sense: !x.sense }); else s.f.delete(r); if (o.outT !== "!cir.bool") s.f.delete(r); break; }
      case "unreachable": case "trap": return;
      case "select": if (ptrT(o.outT)) no("select of pointers"); s.f.delete(r); break;
      case "ptr_diff": nodeOf(a0, "subtracted"); nodeOf(o.args[1], "subtracted"); break;
      case "var_annotation": case "ptr_annotation": { if (o.name === "ptr_annotation") no("pointer annotation"); break; }
      case "lifetime.start": { const v = V.get(a0); if (v?.t === "local" && !ptrLocal.has(v.a)) s.n.set(v.a, { k: "own" }); break; }
      case "lifetime.end": { const v = V.get(a0); if (v?.t !== "local") return no("lifetime"); if (!ptrLocal.has(v.a) || ownedLocal.get(v.a)) killDesc(v.a, "its variable went out of scope"); if (ptrLocal.has(v.a)) { reparent(v.a); s.n.delete(v.a); } else s.n.set(v.a, { k: "dead", why: "its scope ended" }); break; }
      default: if (o.succ.length || o.inT.some(ptrT) || ptrT(o.outT) || /rec_/.test(o.outT)) no(`operation ${o.name} has no rule`); if (!/^(add|sub|mul|div|rem|shift|and|or|xor|inc|dec|minus|plus|binop|unary|max|min|fneg|complement|bit\w*|clz|ctz|popcount|abs|fabs|floor|sqrt|fmuladd|expect|assume|is_constant|is_fp_class|signbit)$/.test(o.name)) no(`operation ${o.name} has no rule`); if (r) s.f.delete(r); }
    } };
  const varName = new Map<string, string>(); const escapes = new Set<string>(); { const al = new Set<string>(); for (const b of f.blocks.values()) for (const o of b.ops) if (o.name === "alloca") al.add(o.res); for (const b of f.blocks.values()) for (const o of b.ops) o.args.forEach((a, i) => { if (al.has(a) && !(o.name === "load" && i === 0) && !(o.name === "store" && i === 1) && !o.name.startsWith("lifetime")) escapes.add(a); }); }
  const pending: [string, "true" | "zero" | ""][] = []; const firstB = (sg: Sig) => sg.params.findIndex(p => p.ptr && !p.owned); const reassigned = new Set<string>();
  // a parameter taken on success must never be assigned again: 'still live' then means the caller's object
  for (const p of sig.params) if (p.taken) { let n = 0; for (const b of f.blocks.values()) for (const o of b.ops) if (o.name === "store") { const al = [...f.blocks.values()].flatMap(q => q.ops).find(q => q.name === "alloca" && q.res === o.args[1] && q.attr.includes(`name = "${p.name}"`)); if (al) n++; } if (n > 1) reassigned.add(p.name); }
  while (work.length) { if (steps++ > 5000) throw new Reject("did not settle"); const bn = work.pop()!; useBlock(bn, clone(inS.get(bn)!)); }
}
if (import.meta.main) { const file = process.argv[2];
  try { const pp = spawnSync(bin + "clang", CC1(file, [], ["-fsyntax-only"])); if (pp.status !== 0) { console.log("clang: " + (pp.stderr.toString().match(/error: .*/)?.[0] ?? "failed")); process.exit(1); }
    const src = readFileSync(file, "utf8").replace(/\/\/.*|\/\*[\s\S]*?\*\//g, " "); const bad = src.match(/_Exclusive|_View\b|_Drop\w*|_Lifetimebound|_Noescape|_Takes\b|_Returns_owned|_Static\b|__unsafe\b|_Unsynchronized|_Unchecked|__attribute__\s*\(\(\s*(?!annotate)|\[\[|_Pragma|#\s*pragma|__builtin_(?!safec_take)|__forge|^\s*#\s*include\s*"/m); if (bad) throw new Reject("outside the subset: " + bad[0].trim());
    const r = check(file); for (const x of r) console.log(x); process.exit(r.length ? 1 : 0); } catch (e) { if (e instanceof Reject) { console.log(e.message); process.exit(1); } console.log("INTERNAL " + (e as Error).stack); process.exit(2); } }
